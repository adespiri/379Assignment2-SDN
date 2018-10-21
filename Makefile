all: a2sdn

clean:
	rm -rf a2sdn
 
a2sdn:	a2sdn.cpp
	g++ a2sdn.cpp -o a2sdn

	
