#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <sqlite3.h>
#include <string.h>

#define PORT 2027
#define MAX_CLIENTS 10
#define MAX_GROUPS 10
#define MAX_GROUP_NAME_LEN 100
#define MAX_USERNAME_LEN 100
#define MAX_MESSAGE_LEN 1024
#define MAX_TIMESTAMP_LEN 100

struct ClientiAcceptati{
	int acceptatiSocketFD;
	bool acceptatSucces;
	struct sockaddr_in address;
	char username[MAX_USERNAME_LEN];
	int idClient;
	bool loginSuccess;
    char currentWindow[100];
};

struct ClientiAcceptati clienti[MAX_CLIENTS];
int nrClienti=0;

struct ClientiAcceptati clienti_logati[MAX_CLIENTS];
int nrClientiLogati=0;

struct Grup {
    int idGrup;
    char nume[MAX_GROUP_NAME_LEN];
    char parola[MAX_GROUP_NAME_LEN];
    int numarMembriLogati;
    int membri[MAX_CLIENTS];
};

struct Grup grupuri[MAX_GROUPS];
int nrGrupuri = 0;

struct GroupMessage {
    char group_name[MAX_GROUP_NAME_LEN];
    char sender_username[MAX_USERNAME_LEN];
    char message[MAX_MESSAGE_LEN];
    char timestamp[MAX_TIMESTAMP_LEN];
};

static int loginCallback(void* data, int argc, char** argv, char** azColName) {
    const char* providedUsername = ((const char**)data)[0];
    const char* providedPassword = ((const char**)data)[1];
	const char* providedNrClient = ((const char**)data)[2];

	int clientIndex = atoi(providedNrClient);

    if (argc == 2) {
        const char* storedUsername = argv[0];
        const char* storedPassword = argv[1];
		printf("%s %s %d; %s %s\n",providedUsername, providedPassword, clientIndex, storedUsername, storedPassword);
        if (strcmp(providedUsername, storedUsername) == 0 && strcmp(providedPassword, storedPassword) == 0) {
            clienti[clientIndex].loginSuccess = true;
			strcpy(clienti[clientIndex].username,providedUsername);
            strcpy(clienti[clientIndex].currentWindow,"login");
            clienti_logati[nrClientiLogati] = clienti[nrClientiLogati];
            nrClientiLogati++;
        }
    }

    return 0;
}

void handleLogin(char* username, char* password, int socketFD, int nrClient) {
    sqlite3* DB;
    int exit = 0;
    exit = sqlite3_open("example.db", &DB);

    if (exit) {
        printf("Error open DB %s\n", sqlite3_errmsg(DB));
        return;
    } else {
        printf("Opened Database Successfully!\n");
    }

	//transformam nrClienti in char
	char nrClientStr[10]="\0";  
    snprintf(nrClientStr, sizeof(nrClientStr), "%d", nrClient);

    const char* data[] = {username, password, nrClientStr};
    const char* sql = "SELECT username, password FROM users;";
    char* errorMessage = 0;

    clienti[nrClient].loginSuccess = false;

    int rc = sqlite3_exec(DB, sql, loginCallback, (void*)data, &errorMessage);

    if (rc != SQLITE_OK) {
        printf("Error SELECT: %s\n", errorMessage);
        sqlite3_free(errorMessage);
    } else {
        printf("Operation OK!\n");
    }

    if (clienti[nrClient].loginSuccess && clienti[nrClient].acceptatiSocketFD==socketFD) {
            write(clienti[nrClient].acceptatiSocketFD, "Login cu succes", 16);
        } else {
            write(clienti[nrClient].acceptatiSocketFD, "Nu ati introdus un username sau parola valide ", 47);
        }

    sqlite3_close(DB);
}

void handleRegister(char* username, char* password, int socketFD, int nrClient) {
    sqlite3* DB;
    int exit = 0;
    exit = sqlite3_open("example.db", &DB);

    if (exit) {
        printf("Error open DB %s\n", sqlite3_errmsg(DB));
        return;
    } else {
        printf("Opened Database Successfully!\n");
    }

    char query[100];
    snprintf(query, sizeof(query), "SELECT * from users WHERE username='%s';", username);
	char* errorMessage = 0;
    sqlite3_stmt* statement;
    int rc = sqlite3_prepare_v2(DB, query, -1, &statement, 0);

    if (rc != SQLITE_OK) {
        printf("[server]Error preparing statement: %s\n", sqlite3_errmsg(DB));
        sqlite3_close(DB);
        return;
    }

    // exista deja username-ul
    int result = 0;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        result = 1;
        break;
    }

    sqlite3_finalize(statement);

    if (result) {
	    // exista deja username-ul
        write(socketFD, "Username-ul exista deja", 24);
    } else {
        // inregistram noul utilizator
        char registerQuery[100];
        snprintf(registerQuery, sizeof(registerQuery), "INSERT INTO users (username, password) VALUES ('%s', '%s');", username, password);

        rc = sqlite3_exec(DB, registerQuery, 0, 0, &errorMessage);

        if (rc != SQLITE_OK) {
            printf("Error INSERT: %s\n", errorMessage);
            write(socketFD, "Eroare la inregistrare", 23);
        } else {
            printf("Inregistrare cu succes!\n");
            write(socketFD, "Inregistrare cu succes", 23);
        }
    }

    sqlite3_close(DB);
}

void handleChangePassword(char* username, char* newPassword, int socketFD) {
    sqlite3* DB;
    int exit = 0;
    exit = sqlite3_open("example.db", &DB);

    if (exit) {
        printf("Error open DB %s\n", sqlite3_errmsg(DB));
        return;
    } else {
        printf("Opened Database Successfully!\n");
    }

    // Check if the provided username and current password match in the database
    char query[100];
    snprintf(query, sizeof(query), "SELECT * from users WHERE username='%s';", username);
    sqlite3_stmt* statement;
    int rc = sqlite3_prepare_v2(DB, query, -1, &statement, 0);

    if (rc != SQLITE_OK) {
        printf("[server]Error preparing statement: %s\n", sqlite3_errmsg(DB));
        sqlite3_close(DB);
        return;
    }

    int result = 0;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        // Username found
        result = 1;
        break;
    }

    sqlite3_finalize(statement);

    if (result) {
        // Update the password for the provided username
        char updateQuery[100];
        snprintf(updateQuery, sizeof(updateQuery), "UPDATE users SET password='%s' WHERE username='%s';", newPassword, username);

        rc = sqlite3_exec(DB, updateQuery, 0, 0, 0);

        if (rc != SQLITE_OK) {
            printf("Error UPDATE: %s\n", sqlite3_errmsg(DB));
            write(socketFD, "Eroare la schimbarea parolei", 29);
        } else {
            printf("Schimbare parola cu succes!\n");
            write(socketFD, "Schimbare parola cu succes", 26);
        }
    } else {
        // Username not found
        write(socketFD, "Username-ul nu exista", 22);
    }

    sqlite3_close(DB);
}

void handleCreateGroup(int socketFD){
    char nume_grup[MAX_GROUP_NAME_LEN]="\0";
    write(socketFD, "Introduceti numele noului grup: ", 31);
    read(socketFD, nume_grup, sizeof(nume_grup) - 1);
    nume_grup[strlen(nume_grup) - 1] = '\0';

    char parola_grup[MAX_GROUP_NAME_LEN]="\0";
    write(socketFD, "Introduceti parola de acces pentru grupul creat: ", 50);
    read(socketFD, parola_grup, sizeof(parola_grup)-1);
    parola_grup[strlen(parola_grup)-1]='\0';

    sqlite3* DB;
    int exit = 0;
    exit = sqlite3_open("example.db", &DB);

    if (exit) {
        printf("Error open DB %s\n", sqlite3_errmsg(DB));
        return;
    }

    char query[100];
    sprintf(query, "INSERT INTO groups (name, password) VALUES ('%s','%s');", nume_grup, parola_grup);
    int rc = sqlite3_exec(DB, query, 0, 0, 0);

    if (rc != SQLITE_OK) {
        printf("Error creating group: %s\n", sqlite3_errmsg(DB));
    }
    int group_id = sqlite3_last_insert_rowid(DB);

    char group_dir[50];
    sprintf(group_dir, "group_%d", group_id);
    mkdir(group_dir, 0777);
    
    if (nrGrupuri < MAX_GROUPS) {
        grupuri[nrGrupuri].idGrup = group_id;
        strcpy(grupuri[nrGrupuri].nume, nume_grup);
        strcpy(grupuri[nrGrupuri].parola, parola_grup);
        nrGrupuri++;
    } else {
        printf("[server]Nu se mai pot crea grupuri. Limita atinsa.\n");
    }

    sqlite3_close(DB);

    write(socketFD, "Grup creat cu succes", 21);
}

void seeAllGroupsCallback(void* data, int argc, char** argv, char** azColName){
    const char* socketFDString = ((const char**)data)[0];
    int socketFD = atoi(socketFDString);

    char groupNames[1024] = ""; 
    for (int i = 0; i < argc; i++) {
        strcat(groupNames, argv[i]);
        strcat(groupNames, "\n");
    }

    write(socketFD, groupNames, strlen(groupNames));
}

void handleSeeAllGroups(int socketFD){
    sqlite3* DB;
    int exit = 0;
    exit = sqlite3_open("example.db", &DB);

    if (exit) {
        printf("Error open DB %s\n", sqlite3_errmsg(DB));
        return (-1);
    } else {
        printf("Opened Database Successfully!\n");
    }
    char socketFDChar[10];
    snprintf(socketFDChar, sizeof(socketFDChar),"%d",socketFD);
    const char* data[] = {socketFDChar};
    const char* sql = "SELECT name FROM groups;";
    char* errorMessage = 0;

    int rc = sqlite3_exec(DB, sql, seeAllGroupsCallback, (void*)data, &errorMessage);

    if (rc != SQLITE_OK) {
        printf("Error SELECT: %s\n", errorMessage);
        sqlite3_free(errorMessage);
    } else {
        printf("Operation OK!\n");
    }

    sqlite3_close(DB);
}

static int displayIstoricMesajeCallback(void* data, int argc, char** argv, char** azColName){
    int socketFD = (int)data;

    char message[1024]="";
    snprintf(message, sizeof(message), "%s: %s", argv[0], argv[1]);
    write(socketFD,message,strlen(message));
    
    return 0;
}

void displayIstoricMesaje(char* groupName, int socketFD){
    sqlite3* DB;
    int exit = 0;
    exit = sqlite3_open("example.db", &DB);

    if (exit) {
        printf("Error open DB %s\n", sqlite3_errmsg(DB));
        return;
    }

    char query[200];
    snprintf(query, sizeof(query), "SELECT sender_username, message FROM group_messages WHERE group_name='%s';", groupName);

    char* errorMessage = 0;

    int rc = sqlite3_exec(DB, query, displayIstoricMesajeCallback, (void*)socketFD, &errorMessage);

    if (rc != SQLITE_OK) {
        printf("Error SELECT: %s\n", errorMessage);
        sqlite3_free(errorMessage);
    }

    // Close the database connection
    sqlite3_close(DB);
}

static int loginGroupCallback(void* data, int argc, char** argv, char** azColName){
    const char* providedGroupName = ((const char**)data)[0];
    const char* providedPassword = ((const char**)data)[1];
    const char* providedNrClient = ((const char**)data)[2];

    printf("unde e segmentation fault: %s\n", providedNrClient);
	int clientIndex = atoi(providedNrClient);
    
    if (argc == 1) {
        const char* storedPassword = argv[0];
		printf("providedPassword, storedPassword: %s %s\n",providedPassword, storedPassword);
        if (strcmp(providedPassword, storedPassword) == 0) {
            strcpy(clienti[clientIndex].currentWindow,providedGroupName);
            clienti_logati[nrClientiLogati] = clienti[nrClientiLogati];
        }
    }
    return 0;
}

int verificareParolaGrup(char* groupName, char* groupPassword, int socketFD, int nrClient){
    sqlite3* DB;
    int exit = 0;
    exit = sqlite3_open("example.db", &DB);

    if (exit) {
        printf("Error open DB %s\n", sqlite3_errmsg(DB));
        return;
    } else {
        printf("Opened Database Successfully!\n");
    }

    char sql[100];
    snprintf(sql, sizeof(sql), "SELECT password FROM groups WHERE name='%s';", groupName);
    
    //transformam nrClienti in char
	char nrClientStr[10]="\0";  
    snprintf(nrClientStr, sizeof(nrClientStr), "%d", nrClient);
    
    const char* data[] = { groupName, groupPassword, nrClientStr };
    char* errorMessage = 0;

    int rc = sqlite3_exec(DB, sql, loginGroupCallback, (void*)data, &errorMessage);

    if (rc != SQLITE_OK) {
        printf("Error SELECT: %s\n", errorMessage);
        sqlite3_free(errorMessage);
    } else {
        printf("Operation OK!\n");
    }

    sqlite3_close(DB);
    if (clienti[nrClient].acceptatiSocketFD==socketFD && strcmp(clienti[nrClient].currentWindow, groupName)==0 ) {
            write(clienti[nrClient].acceptatiSocketFD, "Ati intrat in grup", 19);
            printf("[server] %s\n", clienti[nrClient].currentWindow);
            return 1;
        } else {
            write(clienti[nrClient].acceptatiSocketFD, "Nu ati introdus un username sau parola valide ", 47);
            printf("[server] %s\n", clienti[nrClient].currentWindow);
            return 0;
        }
    
}

void joinGroup(char* groupName, char* groupPassword, char* senderUsername, int socketFD, int nrClient){
    if(verificareParolaGrup(groupName, groupPassword,socketFD, nrClient)==1)
        displayIstoricMesaje(groupName, socketFD);
}

void detectareComenzi(char* buffer, int nrClient, int socketFD){
	if ((strcmp(buffer, "login\n") == 0)&& (clienti[nrClient].loginSuccess==false)) {

		char username[100]="\0";
		char password[100]="\0";

		for(int j=0;j<nrClienti;j++)
			if(clienti[j].acceptatiSocketFD==socketFD){
				write(clienti[j].acceptatiSocketFD, "Introduceti username: ", 22);
			}
        
		read(socketFD, username, sizeof(username)-1);
		username[strlen(username)-1]='\0';
		printf("Acesta este username-ul:%s;",username);
		for(int j=0;j<nrClienti;j++)
			if(clienti[j].acceptatiSocketFD==socketFD){
				write(clienti[j].acceptatiSocketFD, "Introduceti parola: ", 22);
			}
		read(socketFD, password, sizeof(password)-1);
		password[strlen(password)-1]='\0';
		printf("Acesta este password-ul:%s;",password);
		printf("Acesta este nrClient:%d;", nrClient);
		
        // for(int i=0;i<nrClientiLogati;i++){
        //     if()
        // }
		handleLogin(username, password, socketFD, nrClient);

	} else if(strcmp(buffer, "login\n")==0 && clienti[nrClient].loginSuccess==true ){	
		write(socketFD,"Sunteti deja logat",19);
	} else if (strcmp(buffer, "logout\n") == 0 && clienti[nrClient].loginSuccess==true) {
        
		clienti[nrClient].loginSuccess = false;
		write(socketFD, "Logout cu succes", 18);
    
	}else if(strcmp(buffer,"register\n")==0 && clienti[nrClient].loginSuccess==false){
		char username[100] = "\0";
        char password[100] = "\0";

        for (int j = 0; j < nrClienti; j++)
            if (clienti[j].acceptatiSocketFD == socketFD) {
                write(clienti[j].acceptatiSocketFD, "Introduceti username pentru inregistrare: ", 43);
            }
        read(socketFD, username, sizeof(username) - 1);
        username[strlen(username) - 1] = '\0';

        for (int j = 0; j < nrClienti; j++)
            if (clienti[j].acceptatiSocketFD == socketFD) {
                write(clienti[j].acceptatiSocketFD, "Introduceti parola pentru inregistrare: ", 41);
            }
        read(socketFD, password, sizeof(password) - 1);
        password[strlen(password) - 1] = '\0';

        handleRegister(username, password, socketFD, nrClient);
	}else if(strcmp(buffer,"register\n")==0 && clienti[nrClient].loginSuccess==true){
		for (int j = 0; j < nrClienti; j++)
            if (clienti[j].acceptatiSocketFD == socketFD) {
                write(clienti[j].acceptatiSocketFD, "Sunteti deja logat.\n", 47);
            }
	} else if(strcmp(buffer,"change-password\n")==0 && clienti[nrClient].loginSuccess==true){
        char newPassword[100] = "\0";

        for (int j = 0; j < nrClienti; j++)
            if (clienti[j].acceptatiSocketFD == socketFD) {
                write(clienti[j].acceptatiSocketFD, "Introduceti noua parola: ", 26);
            }
        read(socketFD, newPassword, sizeof(newPassword) - 1);
        newPassword[strlen(newPassword) - 1] = '\0';
		printf("Username pentru change-password %s\n",clienti[nrClient].username);
        handleChangePassword(clienti[nrClient].username, newPassword, socketFD);
	} else if(strcmp(buffer,"change-password\n")==0 && clienti[nrClient].loginSuccess==false){
		for (int j = 0; j < nrClienti; j++)
            if (clienti[j].acceptatiSocketFD == socketFD) {
                write(clienti[j].acceptatiSocketFD, "Trebuie sa fiti logat pentru a schimba parola\n", 47);
            }
	} else if(strcmp(buffer, "create-group\n")==0 && clienti[nrClient].loginSuccess==true){
				handleCreateGroup(socketFD);
	} else if(strcmp(buffer, "see-all-groups\n")==0 && clienti[nrClient].loginSuccess==false){
		for (int j = 0; j < nrClienti; j++)
            if (clienti[j].acceptatiSocketFD == socketFD) {
                write(clienti[j].acceptatiSocketFD, "Trebuie sa fiti logat pentru a vedea grupurile existente\n", 47);
            }	
    } else if(strcmp(buffer, "see-all-groups\n")==0 && clienti[nrClient].loginSuccess==true){
        handleSeeAllGroups(socketFD);
    } else if(strcmp(buffer, "get-logged-users\n")==0 && clienti[nrClient].loginSuccess==true){
        char loggedUsersStr[1024] = "";  
        for (int i = 0; i < nrClientiLogati; i++) {
            strcat(loggedUsersStr, clienti_logati[i].username);
            strcat(loggedUsersStr, "\n");
        }
        write(socketFD, loggedUsersStr, strlen(loggedUsersStr));

    } else if(strcmp(buffer, "get-logged-users\n")==0 && clienti[nrClient].loginSuccess==true){
        write(socketFD, "Trebuie sa fiti logat pentru a vizualiza utiliza utilizatorii logati\n", 70);
    } else if (strcmp(buffer, "join-group\n") == 0 && clienti[nrClient].loginSuccess == true) {
        
        char groupName[MAX_GROUP_NAME_LEN] = "\0";
        char groupPassword[MAX_GROUP_NAME_LEN] = "\0";

        write(socketFD, "Introduceti numele grupului: ", 30);
        read(socketFD, groupName, sizeof(groupName) - 1);
        groupName[strlen(groupName) - 1] = '\0';

        write(socketFD, "Introduceti parola grupului: ", 30);
        read(socketFD, groupPassword, sizeof(groupPassword) - 1);
        groupPassword[strlen(groupPassword) - 1] = '\0';

        joinGroup(groupName, groupPassword, clienti[nrClient].username, socketFD, nrClient);
        
        //inputMesajBazaDeDateSiBroadcast(groupName, clienti[nrClient].username, );
    } else if (strcmp(buffer, "join-group\n") == 0 && clienti[nrClient].loginSuccess == false) {
    write(socketFD, "Trebuie sa fiti logat pentru a adera la un grup\n", 49);
    }
}

void handleUploadDatabase(char* groupName, char* nume_fisier, char* path_grup){
    sqlite3* DB;
    int exit = 0;
    exit = sqlite3_open("example.db", &DB);

    if (exit) {
        printf("Error open DB %s\n", sqlite3_errmsg(DB));
        return;
    }

    char sql[200];
    snprintf(sql, sizeof(sql), "INSERT INTO group_files (group_name, file_name, file_path) VALUES ('%s','%s','%s');", groupName, nume_fisier, path_grup);

    char* errorMessage = 0;
    int rc = sqlite3_exec(DB, sql, 0, 0, &errorMessage);

    if (rc != SQLITE_OK) {
        printf("Error INSERT: %s\n", errorMessage);
        sqlite3_free(errorMessage);
    } else {
        printf("File path inserted into database!\n");
    }

    sqlite3_close(DB);
}

static int get_id_grupCallback(void* data, int argc, char** argv, char** azColName){
        if (argc == 1) {
            sscanf(argv[0], "%d", (int*)data);
        }
        return 0;
}

int get_id_grup(char* groupName){
    sqlite3* DB;
    int exit = 0;
    exit = sqlite3_open("example.db", &DB);

    if (exit) {
        printf("Error open DB %s\n", sqlite3_errmsg(DB));
        return -1; 
    }

    char sql[100];
    snprintf(sql, sizeof(sql), "SELECT group_id FROM groups WHERE name='%s';", groupName);

    int group_id = -1; 

    sqlite3_exec(DB, sql, get_id_grupCallback, &group_id, 0);

    sqlite3_close(DB);

    return group_id;
}

void uploadFile(char* groupName, int socketFD){
    char path_fisier[500];  

    write(socketFD, "Introduceti calea absoluta catre fisierul pe care doriti sa il incarcati", 73);
    read(socketFD, path_fisier, sizeof(path_fisier)-1);
    path_fisier[strlen(path_fisier)-1] = '\0';
    printf("Path-ul absolut introdus este: %s\n", path_fisier);
    
    char nume_fisier[100];  
    char* s = strrchr(path_fisier,'/');
    if (s != NULL) {
        strncpy(nume_fisier, s+1, sizeof(nume_fisier)-1);
        nume_fisier[sizeof(nume_fisier) - 1]='\0';
    } 
    printf("Numele fisierului este: %s\n",nume_fisier);

    char continut[5000]="";
    FILE* file = fopen(path_fisier, "rb");
    if (file != NULL) {
    //     printf("se repeta si partea aceasta");
        fseek(file, 0, SEEK_END); 
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        fread(continut, 1, size, file);
        printf("Acesta este dimensiunea fisierului: %d\n", size);
        printf("Acesta este continutul fisierului: %s\n", continut);

        if (size <= sizeof(continut) - 1) {
            fread(continut, 1, size, file);
            continut[size] = '\0'; 

        } else {
            printf("Dimensiunea fisierlui depaseste dimensiunea permisa.\n");
        }

        char path_grup[100]="/home/daria-medeleanu/Desktop/retele/tema2/";
        strcat(path_grup,"group_");
        int groupID = get_id_grup(groupName);
        char groupIDString[3]="";
        snprintf(groupIDString,sizeof(groupIDString),"%d",groupID);
        strcat(path_grup,groupIDString);
        strcat(path_grup,"/");
        strcat(path_grup, nume_fisier);
        printf("Acesta este path-ul unde va fi upload-at: %s\n", path_grup);

        FILE* newFile = fopen(path_grup, "wb");
        if (newFile != NULL) {
            fwrite(continut, sizeof(char), size, newFile);
            fclose(newFile);
            printf("Copiat continut cu succes.\n");
        } else {
            printf("Eroare la deschiderea fisierului\n");
        }

        handleUploadDatabase(groupName, nume_fisier, path_grup);
        
        write(socketFD, "File uploaded successfully!\n", 28);
    } else {
        write(socketFD, "Error opening file. Please check the file path.\n", 48);
    }
}

void seeFilesFromGroup(char* groupName, int socketFD){
    sqlite3* DB;
    int exit = 0;
    exit = sqlite3_open("example.db", &DB);

    if (exit) {
        printf("Error open DB %s\n", sqlite3_errmsg(DB));
        return;
    }

    char sql[200];
    snprintf(sql, sizeof(sql), "SELECT file_name FROM group_files WHERE group_name='%s';", groupName);

    sqlite3_stmt* stmt;

    int rc = sqlite3_prepare_v2(DB, sql, -1, &stmt, NULL);

    if (rc != SQLITE_OK) {
        printf("Error preparing SQL statement: %s\n", sqlite3_errmsg(DB));
        sqlite3_close(DB);
        return;
    }

    char filesFromGroup[500]="Fisierele din grup sunt: ";
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        char* fileName = sqlite3_column_text(stmt, 0);
        strcat(filesFromGroup, fileName);
        strcat(filesFromGroup," ");
    }
    strcat(filesFromGroup,".");
    write(socketFD, filesFromGroup, strlen(filesFromGroup));

    sqlite3_finalize(stmt);
    sqlite3_close(DB);
}

int downloadFileCallback(void* data, int argc, char** argv, char** azColName) {
    char* filePath = (char*)data;

    if (argc == 1) {
        strcpy(filePath, argv[0]);
    }

    return 0;
}

void downloadFile(char* groupName, int socketFD){
    char nume_fisier[100]="";
    write(socketFD, "Introduceti numele fisierului pe care doriti sa il downloadati", 63);
    read(socketFD, nume_fisier,sizeof(nume_fisier)-1);
    nume_fisier[strlen(nume_fisier)-1]='\0';
    printf("Dimensiunea numelui este:%d\n",strlen(nume_fisier));
    printf("Numele fisierului de downlaodat este: %s\n",nume_fisier);//aici va printa de exemplu proiect.txt

    //cautam in baza de date fisierul
    int groupID = get_id_grup(groupName);
    sqlite3* DB;
    int exit = 0;
    exit = sqlite3_open("example.db", &DB);

    if (exit) {
        printf("Error open DB %s\n", sqlite3_errmsg(DB));
        return;
    }

    char sql[200];
    sprintf(sql, "SELECT file_path FROM group_files WHERE id=%d AND file_name='%s';", groupID, nume_fisier);

    char path_fisier[500];
    char* errorMessage = 0;

    int rc = sqlite3_exec(DB, sql, downloadFileCallback, (void*)path_fisier, &errorMessage);

    if (rc != SQLITE_OK) {
        printf("Error SELECT: %s\n", errorMessage);
        sqlite3_free(errorMessage);
        sqlite3_close(DB);
        return;
    }

    sqlite3_close(DB);
    printf("Path fisier: %s\n", path_fisier);
    if (strlen(path_fisier) > 0) {
        //am gasit in group_files fisierul pe care vrem sa il downloadam
        char continut[5000]="";
        FILE* file = fopen(path_fisier, "rb");
        if (file != NULL) {
            fseek(file, 0, SEEK_END); 
            long size = ftell(file);
            fseek(file, 0, SEEK_SET);
            fread(continut, 1, size, file);
            printf("Acesta este dimensiunea fisierului: %d\n", size);
            printf("Acesta este continutul fisierului: %s\n", continut);

            if (size <= sizeof(continut) - 1) {
                fread(continut, 1, size, file);
                continut[size] = '\0'; 

            } else {
                printf("Dimensiunea fisierlui depaseste dimensiunea permisa.\n");
            }

            char path_download[500]="";
            write(socketFD,"Introduceti calea absoluta catre locatia unde doriti sa downloadati fisierul",77);
            read(socketFD,path_download,sizeof(path_download)-1);
            path_download[strlen(path_download)-1]='\0';
            strcat(path_download,"/");
            strcat(path_download,nume_fisier);
            printf("Acesta este path-ul unde va fi downloadat: %s\n", path_download);


            FILE* newFile = fopen(path_download, "wb");
            if (newFile != NULL) {
                fwrite(continut, sizeof(char), size, newFile);
                fclose(newFile);
                printf("Copiat continut cu succes.\n");
            } else {
                printf("Eroare la deschiderea fisierului\n");
            }

            write(socketFD, "Descarcarea fisierului cu succes!", 35);
        
        } else {
                write(socketFD, "Eroare la deschiderea fisierului\n", 34);
        }

    } else {
        write(socketFD, "Fisierul nu a fost gasit in acest grup\n", 29);
    }
}

int detectareComenziDinGrup(char* buffer, int clientIndex, int socketFD, char* groupName){
    printf("Buffer in detectareComenziDingrup este %s\n", buffer);
    if((strcmp(buffer,"upload-file\n")==0)){
        printf("Ai scris upload-file\n");
        uploadFile(groupName, socketFD);
    }else if((strcmp(buffer,"see-files\n")==0)){
        printf("Ai scris see-files\n");
        seeFilesFromGroup(groupName,socketFD);
    }else if((strcmp(buffer,"download-file\n")==0)){
        printf("Ai scris download-file\n");
        downloadFile(groupName,socketFD);
    }
}

void insertGroupMessage(char* groupName, char* senderUsername, char* message){
    sqlite3* DB;
    int exit = 0;
    exit = sqlite3_open("example.db", &DB);

    if (exit) {
        printf("Error open DB %s\n", sqlite3_errmsg(DB));
        return;
    }

    char sql[200];
    snprintf(sql, sizeof(sql), "INSERT INTO group_messages (group_name, sender_username, message) VALUES ('%s','%s','%s');", groupName, senderUsername, message);

    char* errorMessage = 0;
    int rc = sqlite3_exec(DB, sql, 0, 0, &errorMessage);

    if (rc != SQLITE_OK) {
        printf("Error INSERT: %s\n", errorMessage);
        sqlite3_free(errorMessage);
    } else {
        printf("Group message inserted into database!\n");
    }

    sqlite3_close(DB);
}

void trimiteMesajulPrimitLaCeilaltiClienti(char* buffer, int socketFD, char* numeGrup, int idClient){
    for(int i = 0; i<nrClienti;i++)
        if(clienti[i].acceptatiSocketFD!=socketFD && strcmp(clienti[i].currentWindow, numeGrup)==0){ //nu trimit catre mine ci fac broadcast catre ceilalti din grup
            buffer[strlen(buffer)]='\0';
            char mesajDeBroadcast[1024]="";
            strcat(mesajDeBroadcast,clienti[idClient].username);
            strcat(mesajDeBroadcast,": ");
            strcat(mesajDeBroadcast,buffer);
            printf("Mesajul de broadcast este: %s", mesajDeBroadcast);
            write(clienti[i].acceptatiSocketFD,mesajDeBroadcast, strlen(mesajDeBroadcast));
            //inseram in baza de date mesajul
            insertGroupMessage(numeGrup, clienti[idClient].username,buffer);
        
        }
        
    
}

void primitSiPrintatData(int socketFD){
	char buffer[1024]="";
	int bytesRead = 0;
	while(1){
	 	if( (bytesRead = read(socketFD, buffer, sizeof(buffer)-1)) == -1){
	 		printf("[server]Eroare la citire din client\n");
	 		break;
	 	} 
	 	if(bytesRead>0){
			buffer[bytesRead]='\0';
	 		printf("[server]%s\n",buffer);
			fflush(stdout);
			for(int i=0;i<nrClienti;i++)
				if(clienti[i].acceptatiSocketFD==socketFD){
                    printf("[server] window %s\n", clienti[i].currentWindow);
                    if(strcmp(clienti[i].currentWindow,"main")==0 || strcmp(clienti[i].currentWindow,"login")==0){
                        printf("[server] a detectat comanda\n");
                        //displayComenziDisponibile(clienti[i].currentWindow, socketFD);
                        detectareComenzi(buffer, clienti[i].idClient, socketFD);
                    } else { //sunt in grup
                        printf("[server] a detectat mesaj\n");
                        if(strcmp(buffer,"upload-file\n") == 0 || strcmp(buffer,"download-file\n") == 0 || strcmp(buffer,"see-files\n") == 0){
                            // s-a scris o comanda in cadrul grupului
                            printf("s-a scris comanda in cadrul grupului\n");
                            //displayComenziDisponibile(clienti[i].currentWindow, socketFD);
                            detectareComenziDinGrup(buffer, clienti[i].idClient,socketFD,clienti[i].currentWindow);
                        }
                        if((strcmp(buffer,"exit-group\n") == 0) || strcmp(buffer,"upload-file\n") != 0 || strcmp(buffer,"download-file\n") != 0 || strcmp(buffer,"see-files\n") != 0){
                            printf("Ai scris exit-group\n");
                            if(strcmp(buffer,"exit-group\n")!=0){
                                clienti_logati[nrClientiLogati] = clienti[nrClientiLogati];
                                trimiteMesajulPrimitLaCeilaltiClienti(buffer, socketFD, clienti[i].currentWindow, clienti[i].idClient);
                            } else {
                                strcpy(clienti[clienti[i].idClient].currentWindow,"login");
                            }
                        }     
                    }
				}
		}
		if(bytesRead<0){
			break;
		}
	}
	close(socketFD);
}

int main ()
{
    struct sockaddr_in server;	  
    int sd;			

    if ((sd = socket (AF_INET, SOCK_STREAM, 0)) == -1) {
    	perror ("[server]Eroare la socket().\n");
    	return 1;
    }

	int on=1;
	setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	
    bzero(&server, sizeof (server));

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(PORT);

    if (bind (sd, (struct sockaddr *) &server, sizeof (struct sockaddr)) == -1){
    	perror ("[server]Eroare la bind().\n");
    	return 2;
    }

    if (listen (sd, 10) == -1){
    	perror ("[server]Eroare la listen().\n");
    	return 3;
    }
	printf("[server]Astept conexiuni la portul %d.\n", PORT);

	while(true){
		struct sockaddr_in clientAddress;
		int clientAddressSize = sizeof(clientAddress);
		int clientSocketFD = accept(sd,(struct sockaddr *)&clientAddress, &clientAddressSize);
		
		struct ClientiAcceptati* clientSocket = (struct ClientiAcceptati*)malloc(sizeof(struct ClientiAcceptati));
		clientSocket->address = clientAddress;
		clientSocket->acceptatiSocketFD = clientSocketFD;
		clientSocket->acceptatSucces=clientSocketFD>0;
		clientSocket->idClient=nrClienti;
		strcpy(clientSocket->username,"");
        strcpy(clientSocket->currentWindow,"main");
		clienti[nrClienti++]=*clientSocket;
	 	
		pthread_t tid;
		pthread_create(&tid, NULL, primitSiPrintatData, clientSocket->acceptatiSocketFD); 
	}
	
	shutdown(sd, SHUT_RDWR);
	return 0;
    	
}	//main