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
} MSG_RULE; //used for the ADD type

typedef struct {
	int packIP_lo;
	int packIP_hi;
	int port1;
	int port2;
	int switchNumber;

} MSG_PACKET; //used for OPEN. The switch sends its details to the controller, may have to rework this


typedef union { MSG_PACKET packet; MSG_RULE rule; } MSG; //MSG can be an entire packet or rule or empty string to indicate no message
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
	vector<Switch> connectedSwitches;
	vector<int> fifoReadList; //fifo read list contains all fifos that the controller will write to
	vector<int> fifoWriteList; //fifos to write to
} Controller; //controller struct to contain counters

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

void printController(Controller cont)
{ /* This method will print the controller details*/
	printf("Switch Information: \n");
	//for every switch that is connected, print its details

	printf("\n");
	printf("Packet Stats: \n");
	printf("\tReceived:\tOPEN:%d, QUERY:%d\n", cont.openRcvCounter, cont.queryRcvCounter);
	printf("\tTransmitted:\tACK:%d, ADD:%d\n\n", cont.ackSentCounter, cont.addSentCounter);
}

void updateControllerList(Controller* cont, MSG msg)
{

}

void sendAckPacket(int switchNumber, int SCfifo)
{/*	This method is used by the controller to send the acknowledgment packet to the designated switch*/
	FRAME frame;
	frame.kind = ACK;
	write(SCfifo, (char *)&frame, sizeof(frame));
}

void executeController(int numberofSwitches)
{	/* This method will be used for the instance that the controller is chosen*/
	Controller cont;

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
			printController(cont);
		}

		else if (strcmp(usercmd, "exit") == 0)
		{
			printController(cont);
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
				{	//update controller list
					updateControllerList(&cont,frame.msg);

					//send the switch ACK
					cout << "New switch attempting to open" << endl;
					sendAckPacket(frame.msg.packet.switchNumber, cont.fifoWriteList[i]); //the corresponding write FIFO is at the same index as the read FIFO
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

void printFlowTable(Switch sw)
{
	int i = 0; //keeps track of specific rule number
	//this function will print out the switch info to the terminal screen
	printf("Flow Table: \n");
	char actionString[20];
	
	//print out every table in the rulesList
	for (vector<Rule>::iterator itr = sw.rulesList.begin(); itr != sw.rulesList.end(); itr++)
	{
		if (itr->actionType == FORWARD) { strcpy(actionString,"FORWARD"); }
		else if (itr->actionType == DROP) {strcpy(actionString, "DROP");}
		printf("[%d] (srcIP= %d-%d, destIP= %d-%d, action= %s:%d, pri= %d, pktCount=%d \n",
			i,itr->srcIP_lo, itr->srcIP_hi, itr->dstIP_lo, itr->dstIP_hi, actionString, itr->actionVal, itr->pri, itr->pktCount);
		i++;
	}
	printf("\n");
	printf("Packet Stats: \n");
	printf("\t Received:\tADMIT:%d, ACK:%d, ADDRULE:%d, RELAYIN:%d \n", sw.admitCounter, sw.ackCounter, sw.addCounter, sw.relayInCounter);
	printf("\t Transmitted:\tOPEN:%d, QUERY:%d, RELAYOUT:%d \n\n", sw.openCounter, sw.queryCounter, sw.relayOutCounter);
}

MSG composePacketMessage(Switch* sw)
{
	MSG msg;

	msg.packet.port1 = sw->port1;
	msg.packet.port2 = sw->port2;
	msg.packet.packIP_lo = sw->IP_lo;
	msg.packet.packIP_hi = sw->IP_hi;
	msg.packet.switchNumber = sw->switchNumber;

	return msg;
}

void sendOpenPacket(int CSfifo, int SCfifo, Switch* sw)
{ /*this method is called when a switch is initialized, it sends the open packet
  to the controller and waits to receive the ACK packet*/
	struct pollfd poll_list[1]; //help on usign poll from http://www.unixguide.net/unix/programming/2.1.2.shtml
	MSG msg;
	FRAME frame;

	poll_list[0].fd = SCfifo;
	poll_list[0].events = POLLIN;

	msg = composePacketMessage(sw);
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

void executeSwitch(char* filename, int port1, int port2 , int lowIP, int highIP, char* thisSwitch, int switchNum)
{	/* This method will be used for the instance that the switch is chosen*/
	//First initialize the switch object
	Switch sw;
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

				//prompt user for command and then poll
				cout << "Please enter 'list' or 'exit': ";
				cin >> usercmd;
				if (strcmp(usercmd, "list") == 0)
				{	//print out list
					printFlowTable(sw);
				}

				else if (strcmp(usercmd, "exit") == 0)
				{	//print out list and exit
					printFlowTable(sw);
					return;
				}

				//poll
			}
			file.close();
		}

		//once file is done being read we still wait for keystrokes and poll 
		cout << "Please enter 'list' or 'exit': ";
		cin >> usercmd;
		if (strcmp(usercmd, "list") == 0)
		{	//print out list
			printFlowTable(sw);
		}

		else if (strcmp(usercmd, "exit") == 0)
		{	//print out list and exit
			printFlowTable(sw);
			return;
		}
	}
	
}

int main(int argc, char* argv[])
{
	/*There are going to be two categories:
	either the user initiates the program from the view of a controller or they do it from
	the view of a switch
	*/
	char chosenSwitch[100]; //will be used to determine if command line argument was for a switch and not controller
	strcpy(chosenSwitch, argv[1]);
	
	if (strcmp(argv[1], "cont") == 0) //compare if argument entered was cont 
	{
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
			port1num = atoi(&port2[2]);
		}

		executeSwitch(filename, port1num, port2num, lowIP, highIP, chosenSwitch, atoi(&chosenSwitch[2]));

	}

	return 0;
}
