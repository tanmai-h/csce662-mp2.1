
Compile the code using the provided makefile:

    make

To clear the directory (and remove .txt files):
   
    make clean

To run the server without glog messages (port number is optional): 

    ./tsd <-p port>

To run coordinator, server, client (Run them on different terminals)
```bash
./coordinator -p 9090
 ./tsd -c 1 -s 1 -h localhost -k 9090 -p 10000
./tsd  -c 2 -s 1 -h localhost -k 9090 -p 10001
./tsc -h localhost -k 9090 -u 1
./tsc -h localhost -k 9090 -u 2
```
