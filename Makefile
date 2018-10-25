all: a2sdn FIFO

clean:
	rm -rf a2sdn fifo-keyboardcont fifo-keyboardsw1 fifo-keyboardsw2 fifo-keyboardsw3 fifo-keyboardsw4 fifo-keyboardsw5 fifo-keyboardsw6 fifo-keyboardsw7 submit.tar

tar:
	tar -czf submit.tar a2sdn.cpp Makefile A2SDN_ProjectReport.pdf 
 
a2sdn:	a2sdn.cpp
	g++ a2sdn.cpp -o a2sdn

FIFO:
	mkfifo fifo-keyboardcont
	mkfifo fifo-keyboardsw1
	mkfifo fifo-keyboardsw2
	mkfifo fifo-keyboardsw3
	mkfifo fifo-keyboardsw4
	mkfifo fifo-keyboardsw5
	mkfifo fifo-keyboardsw6
	mkfifo fifo-keyboardsw7

