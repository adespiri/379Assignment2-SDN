#include <stdio.h>
#include<iostream>
#include<sys/resource.h>
#include<sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include<vector>
#include<string>
#include<boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h> 
#include <fstream>
#include <poll.h>
#include <signal.h>

#define MAX_NSW 7;
#define MAX_IP 1000;
#define MIN_PRI 4;

using namespace std;

typedef enum {ACK, OPEN, QUERY, ADD, RELAY} KIND; //5 different kinds of packets
typedef enum {DROP, FORWARD } ACTION; //two different kinds of actions

typedef struct {
	int srcIP_lo;
	int srcIP_hi;
	int dstIP_lo;
	int dstIP_hi;
	ACTION actionType;
	int actionVal; //the port to forward to if this field is used
	int pri; //0 highest, 4 lowest
	int pktCount;
} MSG_RULE; //used for the ADD type

typedef struct {
	int packIP_lo;
	int packIP_hi;
	int port1;
	int port2;
	int switchNumber; 

} MSG_PACKET; //used for OPEN. The switch sends its details to the controller, may have to rework this

typedef struct {
	int srcIP;
	int dstIP;
	int port1;
	int port2;
	int switchNumber;
} MSG_QUERY; //message struct that is used when querying for rules from the controller


typedef union { MSG_PACKET packet; MSG_RULE rule; MSG_QUERY query; } MSG; //MSG can be an entire packet or rule or entire switch
typedef struct { KIND kind; MSG msg; } FRAME;

typedef struct {
	int srcIP_lo;
	int srcIP_hi;
	int dstIP_lo;
	int dstIP_hi;
	ACTION actionType;
	int actionVal;
	int pri;
	int pktCount;
} Rule; //rules will be kept in a vector array as part of a switch

typedef struct {
	bool opened;
	int switchNumber;
	char switchIs[10];
	int port1; 
	int port2;
	int IP_lo;
	int IP_hi;
	vector<Rule> rulesList;

	int admitCounter;
	int ackCounter;
	int addCounter;
	int relayInCounter;
	int openCounter;
	int queryCounter;
	int relayOutCounter;

} Switch; //switch struct

typedef struct {
	int openRcvCounter;
	int queryRcvCounter;
	int ackSentCounter;
	int addSentCounter;
	vector<MSG_PACKET> connectedSwitches; //used for when controller acknowledges a new switch
	vector<int> fifoReadList; //fifo read list contains all fifos that the controller will write to
	vector<int> fifoWriteList; //fifos to write to
} Controller; //controller struct to contain counters

Switch* instanceSwitch;
Controller* instanceController;
bool controllerSelected = false;
bool switchSelected = false; //global variables that will be used when USER1 signal is received

void sendFrame(int fd, KIND kind, MSG *msg) //using lab exercise on eclass as a reference
{
	FRAME frame;
	assert(fd >= 0);
	memset((char *)&frame, 0, sizeof(frame));
	frame.kind = kind;
	frame.msg = *msg;
	write(fd, (char *)&frame, sizeof(frame));

}

int openFIFO(int source, int destination)
{	/*This method opens up a fifo determined by its destination switch number (0 for controller) and destination switch number*/
	char fifoString[20];

	strcpy(fifoString, "fifo-x-y");
	fifoString[5] = source + '0'; //convert to a character and replace x with source Number
	fifoString[7] = destination + '0'; //do the same with y and replace it with destination
	return open(fifoString, O_RDWR);
}

FRAME rcvFrame(int fd)
{ /*This function is taken from the lab exercise on eclass and is used to receive frames from FIFOs
	fd is the file descriptor for the opened FIFO*/
	int    len;
	FRAME  frame;

	assert(fd >= 0);
	memset((char *)&frame, 0, sizeof(frame));
	len = read(fd, (char *)&frame, sizeof(frame));
	
	return frame;
}

void printController(Controller* cont)
{ /* This method will print the controller details*/
	printf("Switch Information: \n");
	//for every switch that is connected, print its details
	for (int i = 0; i < cont->connectedSwitches.size(); i++)
	{
		printf("[sw%d] port1= %d, port2= %d, port3= %d-%d\n", cont->connectedSwitches.at(i).switchNumber,
			cont->connectedSwitches.at(i).port1, cont->connectedSwitches.at(i).port2,
			cont->connectedSwitches.at(i).packIP_lo, cont->connectedSwitches.at(i).packIP_hi);
	}

	printf("\n");
	printf("Packet Stats: \n");
	printf("\tReceived:\tOPEN:%d, QUERY:%d\n", cont->openRcvCounter, cont->queryRcvCounter);
	printf("\tTransmitted:\tACK:%d, ADD:%d\n\n", cont->ackSentCounter, cont->addSentCounter);
}

void sendAckPacket(int switchNumber, int SCfifo)
{/*	This method is used by the controller to send the acknowledgment packet to the designated switch*/
	FRAME frame;
	frame.kind = ACK;
	write(SCfifo, (char *)&frame, sizeof(frame));
}

void sendAddPacket(int switchNumber, int SCfifo, MSG* msg)
{	/*this method is used by the controler to send the add packet to the designated switch*/
	FRAME frame;
	frame.kind = ADD;
	frame.msg = *msg;
	write(SCfifo, (char *)&frame, sizeof(frame));

}

MSG createRule(int port1, int port2, int dstIP, int srcIP,Controller cont)
{	/*Method that creates a rule when a switch queries*/
	MSG msg;
	int dstSwitchNumber;
	int actionVal;

	 //iterate through all of the known connected switches and determine if there are any ip ranges that will accommodate the dstIP
	for (int i = 0; i < cont.connectedSwitches.size(); i++)
	{	
		if (cont.connectedSwitches.at(i).packIP_lo <= dstIP && cont.connectedSwitches.at(i).packIP_hi >= dstIP)
		{	//if the dstIP can fit within the switch under observation, then get switch number
			dstSwitchNumber = cont.connectedSwitches.at(i).switchNumber;

			//ASSUMING switches are in numerical order and there is a linear path from one switch to the next
			//we can compare port1 and port2 to the dstSwitch and we should know which direction to sent the packet
			if (port1 >= dstSwitchNumber) { actionVal = 1; } //send to port1 (left)
			else if (port2 <= dstSwitchNumber) { actionVal = 2; } //send to port2 (right)

			//create rule now that we have which port to send packet to
			msg.rule.srcIP_lo = srcIP;
			msg.rule.srcIP_hi = srcIP;
			msg.rule.dstIP_lo = dstIP;
			msg.rule.dstIP_hi = dstIP + 10; //arbitrarily have the range as 10
			msg.rule.actionType = FORWARD;
			msg.rule.actionVal = actionVal;
			msg.rule.pri = 0; //arbitrary
			msg.rule.pktCount = 0;
			return msg;
		}
	}
	//if there is no switch that can accommodate dstIP, then we need to create a DROP rule 
	msg.rule.srcIP_lo = srcIP;
	msg.rule.srcIP_hi = srcIP;
	msg.rule.dstIP_lo = dstIP;
	msg.rule.dstIP_hi = dstIP + 10; //arbitrarily have the range as 10
	msg.rule.actionType = DROP;
	msg.rule.pri = 0; //arbitrary
	msg.rule.pktCount = 0;
	msg.rule.actionVal = 0;

	return msg;
}

void executeController(int numberofSwitches)
{	/* This method will be used for the instance that the controller is chosen*/
	Controller cont;
	instanceController = &cont; //global controller quals cont, FOR USER1SIGNAL handling
	cont.openRcvCounter = 0; //initialize counters
	cont.queryRcvCounter = 0;
	cont.ackSentCounter = 0;
	cont.addSentCounter = 0;

	char usercmd[30];
	cout << "Controller Created" << endl;

	//open up all the FIFOs that are needed (2 * number of switches)
	for (int i = 0; i < numberofSwitches; i++)
	{
		int fdread;
		int fdwrite;
		fdread = openFIFO(i + 1, 0);
		fdwrite = openFIFO(0, i + 1); //zero indexed so we adjust by adding 1 to i
		cont.fifoReadList.push_back(fdread); 
		cont.fifoWriteList.push_back(fdwrite);
	}

	while (1)
	{
		//poll the user for user command
		cout << "Please type 'list' or 'exit': ";
		cin >> usercmd;

		if (strcmp(usercmd, "list") == 0)
		{ 
			printController(&cont);
		}

		else if (strcmp(usercmd, "exit") == 0)
		{
			printController(&cont);
			return;
		}

		else 
		{
			cout << "Invalid Command" << endl; continue;
		}

		//poll the fifos and see if switches are trying to communicate, first initialize poll fd structure
		struct pollfd pollReadList[cont.fifoReadList.size()];

		for (int i = 0; i < cont.fifoReadList.size(); i++)
		{	
			pollReadList[i].fd = cont.fifoReadList.at(i);
			pollReadList[i].events = POLLIN;
		}

		poll(pollReadList, cont.fifoReadList.size(), 0); //do not block, only check if data is being shared across FIFOs
		
		for (int i = 0; i < cont.fifoReadList.size(); i++)
		{
			if ((pollReadList[i].revents&POLLIN) == POLLIN)
			{
				FRAME frame;
				//if there was a request for communication, then receive frame and check what type of packet was sent
				frame = rcvFrame(pollReadList[i].fd);

				if (frame.kind == OPEN)
				{	//update controller list and counter
					cont.connectedSwitches.push_back(frame.msg.packet);
					cont.openRcvCounter += 1;
					//send the switch ACK and increase ACK counter
					cout << "New switch attempting to open" << endl;
					sendAckPacket(frame.msg.packet.switchNumber, cont.fifoWriteList[i]); //the corresponding write FIFO is at the same index as the read FIFO
					cont.ackSentCounter += 1;
				}

				else if (frame.kind == QUERY)
				{
					MSG msg;
					//create new rule based off dstIP and port numbers
					msg = createRule(frame.msg.query.port1, frame.msg.query.port2, frame.msg.query.dstIP, frame.msg.query.srcIP,cont);

					//send rule to switch and increase counters
					cout << "Sending new rule to switch..." << endl;
					sendAddPacket(frame.msg.query.switchNumber, cont.fifoWriteList[i], &msg);
					cont.addSentCounter += 1;
					cont.queryRcvCounter += 1;
				}
			}
		}
	}	
	
	
}

Rule initializeRules(int lowIP, int highIP)
{	//this function will initialize the flow table of the switch with the first default rule
	Rule rule;
	rule.srcIP_lo = 0;
	rule.srcIP_hi = MAX_IP;
	rule.dstIP_lo = lowIP;
	rule.dstIP_hi = highIP;
	rule.actionType = FORWARD;
	rule.actionVal = 3;
	rule.pri = MIN_PRI;
	rule.pktCount = 0;
	return rule;
}

void printFlowTable(Switch* sw)
{
	int i = 0; //keeps track of specific rule number
	//this function will print out the switch info to the terminal screen
	printf("Flow Table: \n");
	char actionString[20];
	
	//print out every table in the rulesList
	for (vector<Rule>::iterator itr = sw->rulesList.begin(); itr != sw->rulesList.end(); itr++)
	{
		if (itr->actionType == FORWARD) { strcpy(actionString,"FORWARD"); }
		else if (itr->actionType == DROP) {strcpy(actionString, "DROP");}
		printf("[%d] (srcIP= %d-%d, destIP= %d-%d, action= %s:%d, pri= %d, pktCount=%d \n",
			i,itr->srcIP_lo, itr->srcIP_hi, itr->dstIP_lo, itr->dstIP_hi, actionString, itr->actionVal, itr->pri, itr->pktCount);
		i++;
	}
	printf("\n");
	printf("Packet Stats: \n");
	printf("\t Received:\tADMIT:%d, ACK:%d, ADDRULE:%d, RELAYIN:%d \n", sw->admitCounter, sw->ackCounter, sw->addCounter, sw->relayInCounter);
	printf("\t Transmitted:\tOPEN:%d, QUERY:%d, RELAYOUT:%d \n\n", sw->openCounter, sw->queryCounter, sw->relayOutCounter);
}

MSG composeOpenMessage(Switch* sw)
{
	MSG msg;

	msg.packet.port1 = sw->port1;
	msg.packet.port2 = sw->port2;
	msg.packet.packIP_lo = sw->IP_lo;
	msg.packet.packIP_hi = sw->IP_hi;
	msg.packet.switchNumber = sw->switchNumber;

	return msg;
}

MSG composeQueryMessage(Switch* sw, int dstIP, int srcIP, int switchNumber)
{ /*Creates the message that contains the necessary information for querying*/
	MSG msg;

	msg.query.srcIP = srcIP;
	msg.query.dstIP = dstIP;
	msg.query.port1 = sw->port1;
	msg.query.port2 = sw->port2;
	msg.query.switchNumber = switchNumber;

	return msg;
}

void sendOpenPacket(int CSfifo, int SCfifo, Switch* sw)
{ /*this method is called when a switch is initialized, it sends the open packet
  to the controller and waits to receive the ACK packet*/
	struct pollfd poll_list[1]; //help on using poll from http://www.unixguide.net/unix/programming/2.1.2.shtml
	MSG msg;
	FRAME frame;

	poll_list[0].fd = SCfifo;
	poll_list[0].events = POLLIN;

	msg = composeOpenMessage(sw);
	//send the frame, indicating it is a packet of type OPEN
	sendFrame(CSfifo, OPEN, &msg);
	//use polling and wait for server to send ACK packet
	printf("Waiting for server to acknowledge...\n");
	poll(poll_list, 1, -1); //wait forever (maybe a bad idea, no?)
	if ((poll_list[0].revents&POLLIN) == POLLIN)
	{
		//server wrote to SCfifo
		frame = rcvFrame(SCfifo);
		if (frame.kind == ACK)
		{	//switch is now opened and connected to controller, increment counters
			sw->openCounter += 1;
			sw->ackCounter += 1;
			sw->opened = true;
			printf("Acknowledgement Received... \n");
			return;
		}

		
	}
	else { printf("error communicating with controller \n"); return; }

}

void sendQueryPacket(int CSfifo, int SCfifo, Switch* sw, int dstIP, int srcIP, int switchNumber)
{ /*this method is called when a switch cannot find a rule for a line in trafficFile, it sends the open packet
  to the controller and waits to receive the ADD packet*/
	struct pollfd poll_list[1];
	MSG msg;
	FRAME frame;

	poll_list[0].fd = SCfifo;
	poll_list[0].events = POLLIN;

	msg = composeQueryMessage(sw, dstIP, srcIP, switchNumber);
	//send the frame, indicating it is a packet of type QUERY
	sendFrame(CSfifo, QUERY, &msg);
	printf("Waiting for server to provide rule...\n");
	poll(poll_list, 1, -1); //wait forever (maybe a bad idea, no?)
	if ((poll_list[0].revents&POLLIN) == POLLIN)
	{
		//server wrote to SCfifo
		frame = rcvFrame(SCfifo);
		if (frame.kind == ADD)
		{	//switch received the new rule, now must apply it
			Rule rule;
			rule.srcIP_hi = frame.msg.rule.srcIP_hi;
			rule.srcIP_lo = frame.msg.rule.srcIP_lo;
			rule.dstIP_hi = frame.msg.rule.dstIP_hi;
			rule.dstIP_lo = frame.msg.rule.dstIP_lo;
			rule.actionType = frame.msg.rule.actionType;
			rule.actionVal = frame.msg.rule.actionVal;
			rule.pri = frame.msg.rule.pri;
			rule.pktCount = frame.msg.rule.pktCount;
			sw->rulesList.push_back(rule); 
			sw->queryCounter += 1;
			sw->addCounter += 1;
			printf("Rule Received... \n");
			return;
		}


	}
	else { printf("error communicating with controller \n"); return; }
}

int checkRuleExists(Switch sw, int dstIP)
{	/*Checks to see if there exists a rule in the switch with the given IPs, returns index of rule in list if it does exist*/
	for (int i = 0; i < sw.rulesList.size(); i++)
	{
		Rule rule;
		rule = sw.rulesList.at(i);
		if (dstIP <= rule.dstIP_hi && dstIP >= rule.dstIP_lo) return i;
	}
	return -1; //-1 indicates rules does not exist
}

void executeSwitch(char* filename, int port1, int port2 , int lowIP, int highIP, char* thisSwitch, int switchNum)
{	/* This method will be used for the instance that the switch is chosen*/
	//First initialize the switch object
	Switch sw;
	instanceSwitch = &sw; //global switch equals sw, FOR USER1SIGNAL handling
	char usercmd[20];
	int CSfifo;
	int SCfifo;
	int p1writeFifo;
	int p1readFifo;
	int p2writeFifo;
	int p2readFifo;

	sw.opened = false;
	sw.port1 =  port1;
	sw.port2 = port2;
	strcpy(sw.switchIs, thisSwitch);
	sw.switchNumber = switchNum;
	sw.IP_lo = lowIP;
	sw.IP_hi = highIP;
	sw.admitCounter = 0;
	sw.ackCounter = 0;
	sw.addCounter = 0;
	sw.relayInCounter = 0;
	sw.openCounter = 0;
	sw.queryCounter = 0;
	sw.relayOutCounter = 0;
	//initialize the first rule 
	sw.rulesList.push_back(initializeRules(lowIP, highIP));

	//open up the FIFOs for this switch/controller pair as well as switch/switch pairs
	CSfifo = openFIFO(sw.switchNumber, 0);
	SCfifo = openFIFO(0, sw.switchNumber);
	
	if (sw.port1 != -1) //-1 indicates null and no connected switch to port 1
	{
		p1writeFifo = openFIFO(sw.switchNumber, sw.port1);
		p1readFifo = openFIFO(sw.port1, sw.switchNumber);
	}

	if (sw.port2 != -1)
	{
		p2writeFifo = openFIFO(sw.switchNumber, sw.port2);
		p2readFifo = openFIFO(sw.port2, sw.switchNumber);
	}

	//send open packet to controller
	sendOpenPacket(CSfifo, SCfifo, &sw);

	string line;
	ifstream file(filename); 

	while (1) 
	{
		if (file.good())
		{
			while (getline(file, line))
			{	
				//ignore any comments or white lines or lines where the switch is not the current switch
				if (line[0] == '#' || line[0] == '\r' || line[0] == '\n') {
					continue;
				}
				else if (strcmp(line.substr(0, line.find(" ")).c_str(), sw.switchIs
				)) continue;
		
				/*tokenize read string and determine if any of the rules for the switch apply TOKENIZING was created in reference to lab material 
				and https://stackoverflow.com/questions/7352099/stdstring-to-char*/
				char cline[100];
				char* temp;
				int dstIP;
				int srcIP;
				int ruleExist;

				strcpy(cline, line.c_str());
		
				temp = strtok(cline, " "); //temp is now switch name
				temp = strtok(NULL, " "); //temp is now srcIP
				srcIP = atoi(temp);
				temp = strtok(NULL, " "); //temp is now dstIP
				dstIP = atoi(temp);

				ruleExist = checkRuleExists(sw, dstIP); //checkRuleExists returns index of rule if it exists, otherwise it returns -1
				//check if there is a rule that exists with these IP ranges
				if (ruleExist == -1) 
				{
					cout << "No rule exists in flow table" << endl;
					//send query packet to server
					sendQueryPacket(CSfifo, SCfifo, &sw, dstIP, srcIP, sw.switchNumber);
				}

				//relay packet

				//prompt user for command and then poll
				cout << "Please enter 'list' or 'exit': ";
				cin >> usercmd;
				if (strcmp(usercmd, "list") == 0)
				{	//print out list
					printFlowTable(&sw);
				}

				else if (strcmp(usercmd, "exit") == 0)
				{	//print out list and exit
					printFlowTable(&sw);
					return;
				}
				else { printf("Invalid Command"); continue; }

				//poll

			}
			file.close();
		}

		//once file is done being read we still wait for keystrokes and poll 
		cout << "Please enter 'list' or 'exit': ";
		cin >> usercmd;
		if (strcmp(usercmd, "list") == 0)
		{	//print out list
			printFlowTable(&sw);
		}

		else if (strcmp(usercmd, "exit") == 0)
		{	//print out list and exit
			printFlowTable(&sw);
			return;
		}
		else { printf("Invalid Command\n"); continue; }
	}
	
}

void user1Handler(int signum)
{	
	printf("\nUSER1 Signal Received... \n");
	if (controllerSelected == true) 
	{ 
		printController(instanceController); 
		printf("Please type 'list' or 'exit: "); 
		fflush(stdout); //use of fflush in signalhandler https://stackoverflow.com/questions/1716296/why-does-printf-not-flush-after-the-call-unless-a-newline-is-in-the-format-strin
		return; 
	}
	else if (switchSelected == true) 
	{ 
		printFlowTable(instanceSwitch); 
		printf("Please type 'list' or 'exit': "); 
		return;
	}
	
}

int main(int argc, char* argv[])
{
	/*There are going to be two categories:
	either the user initiates the program from the view of a controller or they do it from
	the view of a switch
	*/
	signal(SIGUSR1, user1Handler);

	char chosenSwitch[100]; //will be used to determine if command line argument was for a switch and not controller
	strcpy(chosenSwitch, argv[1]);
	
	if (strcmp(argv[1], "cont") == 0) //compare if argument entered was cont 
	{
		controllerSelected = true;
		executeController(atoi(argv[2]));

	}

	else if (chosenSwitch[0] == 's' && chosenSwitch[1] == 'w') //compare if argument was switch
	{
		char* temp;
		int lowIP;
		int highIP;
		char port1[10];
		char port2[10];
		char filename[100];
		int port1num;
		int port2num;

		switchSelected = true;
		strcpy(port1, argv[3]);	//copying main line arguments into actual variables for easier readibility
		strcpy(port2, argv[4]);
		strcpy(filename, argv[2]);
		//parse command to get ip range
		temp = strtok(argv[5], "-");
		lowIP = atoi(temp);
		temp = strtok(NULL, "-");
		highIP = atoi(temp);

		//check if either of the ports is NULL
		if (strcmp(argv[3], "null") == 0)
		{
			port1num = -1;
		}
		else
		{	
			port1num = atoi(&port1[2]);
		}

		if (strcmp(argv[4], "null") == 0)
		{
			port2num = -1;
		}
		else
		{
			port2num = atoi(&port2[2]);
		}

		executeSwitch(filename, port1num, port2num, lowIP, highIP, chosenSwitch, atoi(&chosenSwitch[2]));

	}

	return 0;
}
