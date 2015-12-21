/*
*	tim@inexpro.nl
*	The port number is passed as an argument
*	This version runs forever, forking off a separate
*	process for each connection 
*/
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <sstream>

using namespace std;

void processConnection(int); 
string timestr(time_t);
string execWithOutput(string);
void writeToSocket(int, string);
void error(const char *msg) {
	perror(msg);
	exit(1);
}

int main(int argc, char *argv[]) {
	int sockfd, newsockfd, portno, pid;
	socklen_t clilen;
	struct sockaddr_in serv_addr, cli_addr;
	if (argc < 2) {
		fprintf(stderr, "ERROR, no port provided\n");
		exit(1);
	}
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		error("ERROR opening socket");
	bzero((char *)&serv_addr, sizeof(serv_addr));
	portno = atoi(argv[1]);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(portno);
	if (bind(sockfd, (struct sockaddr *) &serv_addr,
		sizeof(serv_addr)) < 0)
		error("ERROR on binding");
	listen(sockfd, 5);
	clilen = sizeof(cli_addr);
	while (1) {
		newsockfd = accept(sockfd,
			(struct sockaddr *) &cli_addr, &clilen);
		if (newsockfd < 0)
			error("ERROR on accept");
		pid = fork();
		if (pid < 0)
			error("ERROR on fork");
		if (pid == 0) {
			close(sockfd);
			processConnection(newsockfd);
			exit(0);
		}
		else close(newsockfd);
	} /* end of while */
	close(sockfd);
	return 0; /* we never get here */
}

/*
*	There is a separate instance of this function
*	for each connection.  It handles all communication
*	once a connnection has been established.
*/
void processConnection(int sock) {
	int n;
	char buffer[256];

	//time that we will use to store filename
	time_t now = time(0);
	string time = timestr(now);

	//write to socket the filename that we will place later
	writeToSocket(sock, "Connected to ScanKoffer\r\n");
	writeToSocket(sock, "Filename of image will become " + time + ".jpg\r\n");
	writeToSocket(sock, "Waiting for token location command\r\n");

	bzero(buffer, 256);
	n = read(sock, buffer, 255);
	if (n < 0) error("ERROR reading from socket");

	string str(buffer);

	if (std::string::npos != str.find("token")){
		//stepping commands
		string location = str.erase(0, 5);
		writeToSocket(sock, "Token location is on scanner HEX" + location + "\r\n");
		string cmdSteps = "bw_tool --hex 90 41 ";
		cmdSteps.append(location.substr(2, 2));
		cmdSteps.append(" ");
		cmdSteps.append(location.substr(0, 2));
		cmdSteps.append(" 00 00");
		writeToSocket(sock, "Executing command '" + cmdSteps + "'\r\n");
		const char * steppingCmd = cmdSteps.c_str();
		system(steppingCmd);

		//Read where we are
		string currentPosCmd = "bw_tool -a 90 -R 40:i";
		string currentPos = execWithOutput(currentPosCmd);
		string expectedResult = "0000";
		expectedResult.append(location);

		while (std::string::npos == currentPos.find(expectedResult)){
			writeToSocket(sock, "Currend scanner HEX" + currentPos.substr(0, 8) + "\r\n");
			//printf("Current pos = %s\n", currentPos.c_str());
			sleep(1);
			currentPos = execWithOutput(currentPosCmd);
		}
		writeToSocket(sock, "Currend scanner HEX" + currentPos.substr(0, 8) + "\r\n");

		//printf("Going to get %s\n",buffer);
		writeToSocket(sock, "Turning on scanKoffer light\r\n");
		system("bw_tool -a 8e -w 21:ff");
		string command = "raspistill -o ";
		command.append("/home/scanKofferFTPUser/");
		command.append(time.c_str());
		command.append(".jpg");
		//system("raspistill -o %s.jpg", time.c_str());
		writeToSocket(sock, "Taking picture with command '" + command + "'\r\n");
		const char * cmd = command.c_str();
		system(cmd);
		writeToSocket(sock, "Picture saved\r\n");
		writeToSocket(sock, "Turning off scanKoffer light\r\n");
		system("bw_tool -a 8e -w 21:00");

		//Go back to start position
		system("bw_tool --hex 90 41 00 00 00 00");
		writeToSocket(sock, "Returning to start position\r\n");
		currentPos = execWithOutput(currentPosCmd);
		expectedResult = "00000000";
		while (std::string::npos == currentPos.find(expectedResult)){
			writeToSocket(sock, "Currend scanner HEX" + currentPos.substr(0, 8) + "\r\n");
			//printf("Current pos = %s\n", currentPos.c_str());
			sleep(1);
			currentPos = execWithOutput(currentPosCmd);
		}
		writeToSocket(sock, "Currend scanner HEX" + currentPos.substr(0, 8) + "\r\n");
		
		writeToSocket(sock, "Thanking Tim and leaving...\r\n");
		sleep(1);
	}

	//   n = write(sock,"I got your message",18);
	//   if (n < 0) error("ERROR writing to socket");
}

/*
*	Returning string to make filename of
*/
std::string timestr(time_t t){
	std::stringstream strm;
	strm << t;
	return strm.str();
}

/*
*	Execute a system command and grab the output of its return
*/
std::string execWithOutput(string strCommand) {
	const char * cmd = strCommand.c_str();
	FILE* pipe = popen(cmd, "r");
	if (!pipe) return "ERROR";
	char buffer[10];
	std::string result = "";
	while (!feof(pipe)) {
		if (fgets(buffer, 128, pipe) != NULL)
			result += buffer;
	}
	pclose(pipe);
	return result;
}

/*
*	Function to write back to the socket
*/
void writeToSocket(int socket, string message){
	write(socket, message.c_str(), message.length());
}