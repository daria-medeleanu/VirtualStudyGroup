#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <pthread.h>

extern int errno;
int port;

const char* comenziGrup ="upload-file, see-files, download-file, exit-group.\n";
const char* comenziMain ="login, register.\n";
const char* comenziLogin ="logout, change-password,create-group,see-all-groups, get-logged-users, join-group.\n";

void displayComenziDisponibile(){
    char comenzi[700]="";
    
    strcat(comenzi,"Comenzile disponibile pentru fereasta main sunt:\n");
    strcat(comenzi,comenziMain);
    strcat(comenzi,"Comenzile disponibile pentru fereasta login sunt:\n");
    strcat(comenzi,comenziLogin);
    strcat(comenzi,"Comenzile disponibile pentru fereasta grup sunt:\n");
    strcat(comenzi,comenziGrup);

    printf("%s\n",comenzi);
    
}

void ascultaSiPrinteaza(int socketFD){
  char buffer[1024];
  while(1){
    int bitiPrimiti = read(socketFD, buffer, 1024);
    if(bitiPrimiti > 0){
      buffer[bitiPrimiti-1]='\0';
      printf("%s\n", buffer);
    }
    if(bitiPrimiti < 0)
      break;
  }
  close(socketFD);
}

int main (int argc, char *argv[])
{
  int sd;			
  struct sockaddr_in server;	 
  
  if ((sd = socket (AF_INET, SOCK_STREAM, 0)) == -1){
    perror ("Eroare la socket().\n");
    return errno;
  }
  port = atoi (argv[2]);
  
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = inet_addr(argv[1]);
  server.sin_port = htons(port);
  
  if (connect (sd, (struct sockaddr *) &server,sizeof (struct sockaddr)) == -1){
      perror ("[client]Eroare la connect().\n");
      return errno;
  }

  printf("Bine ai venit in grupul de studiu!:)\n");
  fflush(stdout);
  displayComenziDisponibile();
  pthread_t tid;
  pthread_create(&tid, NULL, ascultaSiPrinteaza, sd);

  int bytesWritten = 0;

  while(1){
    char linie[100];
    fgets(linie, 100, stdin);
    linie[strlen(linie)]='\0';
    if( (bytesWritten = write(sd, linie, strlen(linie))) == -1){
      printf("[client]Eroare la scriere spre server\n");
      fflush(stdout);
    }
    if(bytesWritten > 0){
      if(strcmp(linie, "exit\n")==0){
        printf("Ati parasit conversatia:(\n");
        break;
      }
    }
    linie[0]= '\0';
  }
  close(sd);
  
  return 0;
} // main