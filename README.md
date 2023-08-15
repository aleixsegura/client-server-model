# client-server-model
A basic implementation of the traditional client-server model. First assignment of the networking subject from CS degree at University of Lleida.

Usage:    
  1 - Open one terminal for the server and as many terminals as desired clients on a Linux machine.              
  2 - Start the server: $ ./server [ -d ] [ -c <server.cfg> ] [ -u <equips.dat> ]              
  3 - Start client/s ./client [ -d ] [ -c <clientX.cfg> ] [ -f <bootX.cfg> ]    
  4 - Server commands:
  
        - list: displays information about the clients.    
        - quit: finalizes the server.    
  5 - Client commands:
  
        - send-cfg: sends the configuration file to the server.    
        - get-cfg: gets the configuration file from the server.    
        - quit: finalizes the client.    
