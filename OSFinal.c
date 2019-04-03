#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <sys/select.h>

#include<pthread.h> //thread

#include <sys/stat.h>
#include <fcntl.h>

#define	QLEN			5
#define	BUFSIZE			4096

pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t c = PTHREAD_COND_INITIALIZER;

pthread_mutex_t lock;

int passivesock( char *service, char *protocol, int qlen, int *rport );

typedef struct {
	char topic[BUFSIZE];
	char group_name[BUFSIZE];
	int group_size;
	int curr_group_size;

	fd_set group_set;
	fd_set temp_set;
	int max_fd;

	int quiz_size;
	char quiz_questions[BUFSIZE]; 
	char filename[BUFSIZE];

	int fd_clients[1014];
	int ans_fd_clients[1014];

	int quiz_started;

	int answered;

	char champion[BUFSIZE];
	char results[BUFSIZE];
	char fastest_answer[BUFSIZE];

} Groups;

typedef struct {
	char name[BUFSIZE];
	int admin;
	int joined_group_num;
	//int takingExam; //boolean

	int score;
} Clients;

Groups group[32];
Clients client[1010];

int freeGroup() {
	for (int i = 0; i < 32; ++i)
	{
		if(strlen(group[i].group_name) == 0) 
		{
			return i;
		}
	}
	return -1;
}

void listOfOpenGroups(int ssock) {
	char grouplist[BUFSIZE];
	strcpy(grouplist, "OPENGROUPS");
	//LIST OF OPEN GROUPS
	for (int i=0; i < 32; ++i)
	{
		if((strlen(group[i].group_name) == 0) || group[i].quiz_started == 1) 
		{
			continue;
		}
		sprintf(grouplist + strlen(grouplist), "|%s|%s|%d|%d", group[i].topic, 
			group[i].group_name, group[i].group_size, group[i].curr_group_size);
	}

	strcat(grouplist, "\r\n");
	write(ssock, grouplist, strlen(grouplist));
	memset(grouplist, 0, BUFSIZE);

}

//thread
void *newFile(void *);
void *newGroup(void *);


fd_set			afds;


/*
**	The server ...
*/
int
main( int argc, char *argv[] )
{
	char			buf[BUFSIZE];
	char			*service;
	struct sockaddr_in	fsin;
	int			msock;
	int			ssock;
	fd_set			rfds;
	int			alen;
	int			fd;
	int			nfds;
	int			rport = 0;
	int			cc;

	char *token;

	pthread_t thread_id;
	pthread_t thread_id1;

	char end[BUFSIZE] = "ENDGROUP";

	switch (argc) 
	{
		case	1:
			// No args? let the OS choose a port and tell the user
			rport = 1;
			break;
		case	2:
			// User provides a port? then use it
			service = argv[1];
			break;
		default:
			fprintf( stderr, "usage: server [port]\n" );
			exit(-1);
	}

	msock = passivesock( service, "tcp", QLEN, &rport );
	if (rport)
	{
		//	Tell the user the selected port
		printf( "server: port %d\n", rport );	
		fflush( stdout );
	}

	
	// Set the max file descriptor being monitored
	nfds = msock+1;

	FD_ZERO(&afds);
	FD_SET( msock, &afds );

	for (;;)
	{
		// Reset the file descriptors you are interested in
		memcpy((char *)&rfds, (char *)&afds, sizeof(rfds));

		// Only waiting for sockets who are ready to read
		//  - this also includes the close event
		if (select(nfds, &rfds, (fd_set *)0, (fd_set *)0,
				(struct timeval *)0) < 0)
		{
			fprintf( stderr, "server select: %s\n", strerror(errno) );
			exit(-1);
		}

		// Since we've reached here it means one or more of our sockets has something
		// that is ready to read

		// The main socket is ready - it means a new client has arrived
		if (FD_ISSET( msock, &rfds)) 
		{
			int	ssock;

			// we can call accept with no fear of blocking
			alen = sizeof(fsin);
			ssock = accept( msock, (struct sockaddr *)&fsin, &alen );
			if (ssock < 0)
			{
				fprintf( stderr, "accept: %s\n", strerror(errno) );
				exit(-1);
			}

			//new client
			strcpy(client[ssock-4].name, "");
			client[ssock-4].admin = 0;
			client[ssock-4].joined_group_num = -1;
			client[ssock-4].score = 0;

			//LIST OF OPEN GROUPS
			listOfOpenGroups(ssock);
			
			/* start listening to this guy */
			FD_SET( ssock, &afds );

			// increase the maximum
			if ( ssock+1 > nfds )
				nfds = ssock+1;
		}

		/*	Handle the participants requests  */
		for ( fd = 0; fd < nfds; fd++ )
		{

			// check every socket to see if it's in the ready set
			if (fd != msock && FD_ISSET(fd, &rfds))
			{

				
				// read without blocking because data is there
				if ( (cc = read( fd, buf, BUFSIZE )) <= 0 )
				{
					printf( "The client has gone.\n" );
					(void) close(fd);
					FD_CLR( fd, &afds );
					// lower the max socket number if needed
					if ( nfds == fd+1 )
						nfds--;

				}
				else
				{
					buf[cc] = '\0';
					printf("Client with fd=%i says: %s\n", fd, buf);
                    token = strtok(buf, "|");


					//GETOPENGROUPS
					if(strncmp(token, "GETOPENGROUPS", 13) == 0) {
						listOfOpenGroups(fd);
					}

					//GROUP
					if (strncmp(token, "GROUP", 5) == 0) {
						FD_CLR(fd, &afds);

						int temp = freeGroup(); 

						if (temp == -1)
						{
							write(fd, "BAD|GROUPS are more than 32\r\n", strlen("BAD|GROUPS are more than 32\r\n"));
							FD_SET(fd, &afds);
							continue;
						}
					
						char topic[BUFSIZE];
						char name[BUFSIZE];
						int group_size;

						if ((token = strtok(NULL, "|")))        		
							strcpy(topic, token);
						else {
							write(fd, "BAD|Wrong Input\r\n", strlen("BAD|Wrong Input\r\n"));
							continue;
						}

						if ((token = strtok(NULL, "|")))      		
							strcpy(name, token);
						else {					
							write(fd, "BAD|Wrong Input\r\n", strlen("BAD|Wrong Input\r\n"));
							continue;
						}

						if ((token = strtok(NULL, "|"))){
							group_size = atoi(token);
							if (group_size <= 0)
							{
								write(fd, "BAD|Wrong Input\r\n", strlen("BAD|Wrong Input\r\n"));
								continue;	
							}
						}	        			
						else {
							write(fd, "BAD|Wrong Input\r\n", strlen("BAD|Wrong Input\r\n"));
							continue;
						}
		
						int w = 0;

						for(int i=0; i<32; i++) {
							if(strcmp(group[i].group_name, name) == 0) 
							{
								w++;
								break;
							}
						}

						if (w==0) 
						{
							strcpy(group[temp].topic, topic);
							strcpy(group[temp].group_name, name);
							group[temp].group_size = group_size;

							group[temp].curr_group_size = 0;
							group[temp].quiz_started = 0;
							group[temp].max_fd = 0;
							group[temp].answered = 0;

							client[fd-4].admin = 1;
							client[fd-4].joined_group_num = temp;

							group[temp].fd_clients[fd] = fd;

							printf("%s|%s|%i\n", group[temp].topic, group[temp].group_name, group[temp].group_size);

							write(fd, "SENDQUIZ\r\n", strlen("SENDQUIZ\r\n"));

							//printf("SENDQUIZ\n");

							//create a thread to read a file
							pthread_create( &thread_id, NULL, newFile, (void*) &fd);
							pthread_join(thread_id, NULL);

							FD_SET(fd, &afds);

						} 
						else 
						{
							write(fd, "BAD|Such group is already exits\r\n", strlen("BAD|Such group is already exits\r\n"));
							FD_SET(fd, &afds);
						}						
					}

					//JOIN
					if (strncmp(token, "JOIN", 4) == 0)
					{
						if (client[fd-4].admin)
						{
							write(fd, "BAD|You're an admin\r\n", strlen("BAD|You're an admin\r\n"));
							continue;
						}
						if (token = strtok(NULL, "|")) {

							int temp = -1;

							for (int i = 0; i < 32; ++i)
							{
								if (strcmp(token, group[i].group_name) == 0)
								{
									temp = i;
								}
							}

							if (temp != -1)
							{
								if (group[temp].quiz_started == 1)
								{
									write(fd, "FULL\r\n", strlen("FULL\r\n"));
									continue;
								}

								group[temp].fd_clients[fd] = fd;

								group[temp].curr_group_size++;
								
								if (group[temp].curr_group_size == group[temp].group_size)
									group[temp].quiz_started = 1;
								
								client[fd-4].joined_group_num = temp;

								token = strtok(NULL, "|");
								token[strlen(token) - 2] = '\0';

								strncpy(client[fd-4].name, token, strlen(token));

								if (group[temp].max_fd < fd)
								{
									group[temp].max_fd = fd;
								}
								write(fd, "OK\r\n", strlen("OK\r\n"));
								write(fd, "WAIT\r\n", strlen("WAIT\r\n"));

								//last client creates a group
								if (group[temp].curr_group_size == group[temp].group_size)
								{			
									for (int i = 0; i < 1014; ++i)
									{
										if (group[temp].fd_clients[i] == 0)
										{
											continue;
										} else {
											FD_CLR(i, &afds);
											FD_SET(i, &group[temp].group_set);
										}
										
									}
									group[temp].quiz_started = 1;								
									//create a thread for a new group
									pthread_create( &thread_id1, NULL, newGroup, (void*) &temp);
								}


							} else {
								write(fd, "NOGROUP\r\n", strlen("NOGROUP\r\n"));
							}

						}       		
							
					}

					//LEAVE
					if (strncmp(token, "LEAVE", 5) == 0)
					{
						if (client[fd-4].admin)
						{
							write(fd, "BAD|You're an admin\r\n", strlen("BAD|You're an admin\r\n"));
							continue;
						}

						if (client[fd-4].joined_group_num != -1)
						{
							group[client[fd-4].joined_group_num].curr_group_size--;
							FD_CLR(fd, &group[client[fd-4].joined_group_num].group_set);
							client[fd-4].joined_group_num = -1;
							write(fd, "OK\r\n", strlen("OK\r\n"));
						} else {
							write(fd, "BAD|You don't have a group\r\n", strlen("BAD|You don't have a group\r\n"));
						}
					}

					//CANCEL
					if (strncmp(token, "CANCEL", 6) == 0)
					{
						if (client[fd-4].admin)
						{
							token = strtok(NULL, "|");

							int temp = -1;

							for (int i = 0; i < 32; ++i)
							{
								if (strcmp(token, group[i].group_name) == 0)
								{
									temp = i;
								}
							}

							if (temp != -1)
							{
								if (client[fd-4].joined_group_num != temp)
								{
									write(fd, "BAD|You're not an admin of this group\r\n", strlen("BAD|You're not an admin of this group\r\n"));
									continue;
								}

								client[fd-4].admin = 0;
								client[fd-4].joined_group_num = -1;

								sprintf(end + strlen(end), "|%s\r\n", group[temp].group_name);

								for (int i = 0; i < 1014; ++i)
								{
									if (group[temp].fd_clients[i] == 0)
										continue;
									else
										write(group[temp].fd_clients[i], end, strlen(end));
								}

								FD_ZERO(&group[temp].group_set);

								strcpy(group[temp].topic, "");
								strcpy(group[temp].group_name, "");
								group[temp].group_size = 0;

								group[temp].curr_group_size = 0;
								group[temp].quiz_started = 0;
								group[temp].max_fd = 0;
								group[temp].answered = 0;

								for (int i = 0; i < 1014; ++i)
								{
									group[temp].fd_clients[i] = 0;
								}

								write(fd, "OK\r\n", strlen("OK\r\n"));

							} else {
								write(fd, "BAD|No such group\r\n", strlen("BAD|No such group\r\n"));
							}

						} else {
							write(fd, "BAD|You're not an admin\r\n", strlen("BAD|You're not an admin\r\n"));
						}
						

					}



				}

				
			}

		}
	}
}

//thread

void *newFile(void *socket_desc)
{
	int sock = *(int*)socket_desc;

	int cc;
	char *tFile;

	char buff[BUFSIZE];

	// read without blocking because data is there
	if ( (cc = read( sock, buff, BUFSIZE )) <= 0 )
	{
		write(sock, "BAD\r\n", strlen("BAD\r\n"));
		//close(sock);
		//kill a thread
	}
	else
	{

		buff[cc] = '\0';
		char *rest = buff;

		printf("NOT Bad\n");
		tFile = strtok_r(rest, "|", &rest);
		tFile[strlen(tFile)] = '\0';

		group[client[sock-4].joined_group_num].quiz_size = atoi(strtok_r(rest, "|", &rest));
		int rem;
		rem = group[client[sock-4].joined_group_num].quiz_size;

		//save quiz questions
		sprintf(group[client[sock-4].joined_group_num].filename, "%i.txt", client[sock-4].joined_group_num);
		remove(group[client[sock-4].joined_group_num].filename);

		int fds;						
		fds = open(group[client[sock-4].joined_group_num].filename, O_CREAT | O_WRONLY, 0600);

		if(fds == -1) {
			printf("Failed to create a file\n");
			exit(1);
		}

		while (rem>0) {
			cc = read(sock, buff, BUFSIZE);
			buff[cc] = '\0';

			write(fds, buff, strlen(buff));
			rem -= cc;

		}

		close(fds);

		write(sock, "OK\r\n", strlen("OK\r\n"));
		printf("Finished\n");
								 
	}

	return 0;
}

void *newGroup(void *group_index)
{
	printf("New quiz group is started\n");

	int temp = *(int*)group_index;

	FILE *fp;
	char line[BUFSIZE];
	char buff[BUFSIZE];
	char ques[BUFSIZE];
	char win[BUFSIZE];
	char results[BUFSIZE] = "RESULT";
	
	char end[BUFSIZE] = "ENDGROUP";

	sprintf(end + strlen(end), "|%s\r\n", group[temp].group_name);

	int cc;
	int sret;
	struct timeval timeout;

	char *token;

	fp = fopen(group[temp].filename, "r");

	printf("Quiz start\n");

	int nfds = group[temp].max_fd + 1;

	
	while(!feof(fp)) {	

		if (group[temp].curr_group_size == 0)
		{
			break;
		}

		fgets(line, BUFSIZE, fp);
		if (strlen(line) == 1 || strlen(line) == 0)
		{
			break;
		}

		line[strlen(line) - 1] = '\0';

		sprintf(ques, "QUES|%lu|%s", strlen(line), line);

		for (int i = 0; i < 1014; ++i)
		{
			if (group[temp].fd_clients[i] == 0)
				continue;
			else
				write(group[temp].fd_clients[i], ques, strlen(ques));
		}

		memset(line, 0, BUFSIZE);
		memset(ques, 0, BUFSIZE);

		while(1) {

			fgets(line, BUFSIZE, fp);
			if (strlen(line) == 1 || strlen(line) == 0)
			{
				break;
			}

			line[strlen(line) - 1] = '\0';

			sprintf(ques, "QUES|%lu|%s", strlen(line), line);

			for (int i = 0; i < 1014; ++i)
			{
				if (group[temp].fd_clients[i] == 0)
					continue;
				else
					write(group[temp].fd_clients[i], ques, strlen(ques));
			}

			memset(line, 0, BUFSIZE);
			memset(ques, 0, BUFSIZE);

		}


		memset(line, 0, BUFSIZE);
		memset(ques, 0, BUFSIZE);

		//get Answer
		fgets(line, BUFSIZE, fp);
		line[strlen(line) - 1] = '\0';
		
		timeout.tv_sec = 60;
		timeout.tv_usec = 0;
		printf("Wait their answer\n");

		group[temp].answered = 0;
		

		///////////////////////////////////////////////////////////

		while(group[temp].answered < group[temp].curr_group_size) {
			if (group[temp].curr_group_size == 0)
			{
				break;
			}

			memcpy((char *)&group[temp].temp_set, (char *) &group[temp].group_set, sizeof(group[temp].temp_set));

			sret = select(nfds, &group[temp].temp_set, (fd_set *)0, (fd_set *)0, &timeout);

			if (sret < 0)
			{
				printf("Error\n");
				break;
			} else if (sret == 0)
			{
				printf("timeout\n");
				for (int i = 0; i < 1014; ++i)
				{
					if (group[temp].fd_clients[i] != 0 && group[temp].ans_fd_clients[i] == 0 && !client[i-4].admin)
					{
						printf("Didn't answer: %i\n", i);
						group[temp].fd_clients[i] = 0;
						client[i-4].joined_group_num = -1;
						strcpy(client[i-4].name, "");

						pthread_mutex_lock(&lock);
						group[temp].curr_group_size--;

						write(i, end, strlen(end));
						FD_CLR(i, &group[temp].group_set);
						FD_SET(i, &afds);

						pthread_mutex_unlock(&lock);
					}
				}
				break;
			} else {
				for (int i = 0; i < nfds; ++i)
				{
					if (FD_ISSET(i, &group[temp].temp_set))
					{

						if ( (cc = read(i, buff, BUFSIZE )) <= 0 )
						{
							printf( "The client has gone.\n" );
							(void) close(i);
							FD_CLR(i, &group[temp].group_set);
						}

						char *rest = buff;

						token = strtok_r(rest, "|", &rest);

						if (strncmp(token, "CANCEL", 6) == 0)
						{
							token = strtok_r(rest, "|", &rest);
							//remove CRLF
							token[strlen(token) - 2] = '\0';

							if (client[i-4].admin)
							{
								if (client[i-4].joined_group_num == temp && strncmp(token, group[temp].group_name, strlen(token)) == 0)
								{
									write(i, end, strlen(end));
									group[temp].fd_clients[i]=0;

									pthread_mutex_lock(&lock);
									FD_CLR(i, &group[temp].group_set);
									FD_SET(i, &afds);
									pthread_mutex_unlock(&lock);
								} 
								else 
								{
									write(i, "BAD|WRONG GROUP NAME\r\n", strlen("BAD|WRONG GROUP NAME\r\n"));
								}
							} 
							else 
							{
								write(i, "BAD|You're not an admin\r\n", strlen("BAD|You're not an admin\r\n"));
							}

						} 
						else if (strncmp(token, "ANS", 3) == 0)
						{

							token = strtok_r(rest, "|", &rest);
							token[strlen(token) - 2] = '\0';

							if (client[i-4].admin)
							{
								continue;
							}

							pthread_mutex_lock(&lock);
							group[temp].answered++;
							pthread_mutex_unlock(&lock);

							if (strncmp(token, "NOANS", 5) == 0)
							{
								printf("NOANS\n");
							} else if (strncmp(line, token, strlen(token)) == 0 && strcmp(group[temp].champion, "") == 0 )
							{
								printf("Answered first\n");
								strcpy(group[temp].champion, buff);
								strcpy(group[temp].fastest_answer, client[i-4].name);

								pthread_mutex_lock(&lock);
								client[i-4].score += 2;
								pthread_mutex_unlock(&lock);

								printf("Fastest: %s\n", group[temp].fastest_answer);
							} else if (strncmp(line, token, strlen(token)) == 0)
							{
								printf("Correct\n");

								pthread_mutex_lock(&lock);
								client[i-4].score += 1;
								pthread_mutex_unlock(&lock);
							} else {
								printf("Not Correct\n");

								pthread_mutex_lock(&lock);
								client[i-4].score -= 1;
								pthread_mutex_unlock(&lock);
								
							}	
							group[temp].ans_fd_clients[i] = 1;
						}

						

						

					}
				}
				
			}

		}

		strcpy(group[temp].champion, "");

		
		sprintf(win, "WIN|%s\r\n", group[temp].fastest_answer);


		for (int i = 0; i < 1014; ++i)
		{
			if (group[temp].fd_clients[i] == 0)
				continue;
			else
				write(group[temp].fd_clients[i], win, strlen(win));
		}

		for (int i = 0; i < 1014; ++i)
		{
			group[temp].ans_fd_clients[i] = 0;
		}

		strcpy(group[temp].fastest_answer, "");
		
		/////////////////////////////////////////////////////////////
		memset(line, 0, BUFSIZE);

		fgets(line, BUFSIZE, fp);
		memset(line, 0, BUFSIZE);

	}



	
	//Results
	int temp_fd_score[1014];
	int temp_size = 0;

	for (int i = 0; i < 1014; ++i)
	{
		if (group[temp].fd_clients[i] == 0)
		{
			continue;
		}

		temp_fd_score[temp_size] = group[temp].fd_clients[i];
		temp_size++;
	}



	for (int i = 0; i < temp_size; ++i)
	{
		for (int j = 0; j <temp_size; ++j)
		{

				if (client[temp_fd_score[j]-4].score < client[temp_fd_score[i]-4].score)
				{
					int tmp = temp_fd_score[j];
					temp_fd_score[j] = temp_fd_score[i];
					temp_fd_score[i] = tmp;
				}
			
		}
	}

	for (int i = 0; i < temp_size; ++i)
	{
		printf(" %i ", temp_fd_score[i]);
	}

	printf("\n");
	for (int i = 0; i < temp_size; ++i)
	{
		if (client[temp_fd_score[i]-4].admin)
			continue;
		else
			sprintf(results + strlen(results), "|%s|%i", client[temp_fd_score[i]-4].name, client[temp_fd_score[i]-4].score);
	}



/*
	for (int i = 0; i < 1014; ++i)
	{
		if (group[temp].fd_clients[i] == 0)
			continue;
		else if (client[i-4].admin)
			continue;
		else
			sprintf(results + strlen(results), "|%s|%i", client[i-4].name, client[i-4].score);
	}
*/
	strcat(results, "\r\n");

	for (int i = 0; i < 1014; ++i)
	{
		if (group[temp].fd_clients[i] == 0)
			continue;
		else
			write(group[temp].fd_clients[i], results, strlen(results));
	}

	//ENDGROUP

	for (int i = 0; i < 1014; ++i)
	{
		if (group[temp].fd_clients[i] == 0)
			continue;
		else
			write(group[temp].fd_clients[i], end, strlen(end));
	}

	FD_ZERO(&group[temp].group_set);

	strcpy(group[temp].topic, "");
	strcpy(group[temp].group_name, "");
	group[temp].group_size = 0;

	group[temp].curr_group_size = 0;
	group[temp].quiz_started = 0;
	group[temp].max_fd = 0;
	group[temp].answered = 0;

	for (int i = 0; i < 1014; ++i)
	{
		if (group[temp].fd_clients[i] == 0)
		{
			continue;
		} else if(client[i-4].admin) {
			client[i-4].admin = 0;
		}

		strcpy(client[i-4].name, "");
		client[i-4].joined_group_num = -1;
		FD_SET(i, &afds );
	}

	for (int i = 0; i < 1014; ++i)
	{
		group[temp].fd_clients[i] = 0;
	}


	return 0;

}
