#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>

#include <string.h>
#include <time.h>
#include <netdb.h>
#include <signal.h>

#include <errno.h>
#include <pthread.h>

#include "strutture.h"		// Header file con le strutture e le funzioni comuni da me definite


// Dichiarazione delle funzioni usate
void gestisci_invio_messaggi(struct my_client* client);

void gestisci_ricezione_messaggi(struct my_client *client);

void gestisci_comunicazioni(struct my_client* client);

void chiudi_client();



/*
 *  Main per l'esecuzione del client
 */
int main(int argc, char *args[])	// In args[1] c'è l'ip del server, in args[2] ci sta il numero di porta
{
	
	int clientFd, port_num;			// Fd per la unnamed socket del client, numero di porta

	struct sockaddr_in server_addr;		// Struttura per il server_addr

	struct my_client *client;		// Struttura che contiene i dati del client

	char nome[NAME_MAX_LEN];		// Per contenere il nome dell'utente


	// Definisco la struttura che mi servirà per gestire i segnali
	struct sigaction act;
	sigset_t set;

	// Verifico che gli input dati al programma sono tanti quanto quelli attesi
	if (argc != 3){
		printf("Input attesi: comando < ip > < porta > \n");
		exit(EXIT_FAILURE);
	}



	// Definisco la unnamed socket che usa il client per comunicare con il server
	clientFd = socket(AF_INET, SOCK_STREAM, 0);
	if (clientFd < 0)
	{
		perror("Errore nell'apertura della socket: ");
		exit(EXIT_FAILURE);
	}
	

	// Converto ad intero il numero di porta
	port_num = atoi(args[2]);


	// Imposto i campi della  struttura server_addr
	server_addr.sin_family = AF_INET;			// Per usare l'IPV4
	server_addr.sin_port = htons(port_num);			// Converto in formato network il numero di porta
	
	struct hostent *host = gethostbyname(args[1]);			// Per produrre la struttura che contiene l'ip (in formato network) del server
	server_addr.sin_addr.s_addr = *((long *)host-> h_addr);		// a cui vogliamo connetterci. E per impostarlo in server_addr

	
	// Connessione con il server
	if ( connect(clientFd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
	{
		perror("Errore nella connessione con il server: " );
		exit(EXIT_FAILURE);
	}


	// Invito l'utente a inserire il nome utente
	printf("Inserisci il tuo nome utente: ");

	//NOTA: questo do-While serve ad avere la garanzia che un utente che vuole connettersi alla chat scelga un nome utente
	//	diverso da tutti quelli scelti dai client già connessi alla chat.
	//	In questa fase, l'utente può scegliere se uscire con exit oppure EXIT, ma NON può chiudere il client con un SIGINT
	
	signal(SIGINT, SIG_IGN);


	do {
		// Prendo dallo stdin il nome dell'utente e lo salvo in nome
		fgets(nome, NAME_MAX_LEN, stdin);

	
		// Riduco la dimensione del nome a quella realmente inserita dall'utente
		trim_messaggio(nome);

		// Verifico che la lunghezza del nome sia corretta
		if (strlen(nome) > NAME_MAX_LEN || strlen(nome) <= 1){
			printf("Il nome è troppo lungo oppure troppo corto! \nDeve essere compreso:  2 <= nome <= 20");
			exit(EXIT_FAILURE);
		}

		// Mando il nome del client sul fd con cui comunico per verificare che il nome sia valido
		int errCheck = send(clientFd, nome, NAME_MAX_LEN, 0);

		if ( errCheck < 0 ){
			perror("Errore nell'inviare il nome al server: ");
			exit(EXIT_FAILURE);
		}
		else if(strcmp(nome, "exit") == 0 || strcmp(nome, "EXIT") == 0)
		{
			// Se ha mandato la richiesta di uscita, allora termino il client
			printf("\n============= ARRIVEDERCI ============\n");
			return EXIT_SUCCESS;
		}

		char messaggio[MAX_LEN];

		// Leggo la risposta del server ( per capire se il nome è valido o meno )
		errCheck = recv(clientFd, messaggio, MAX_LEN, 0);

		if ( errCheck < 0 ){
			perror("Errore nel leggere la risposta del server: ");
			exit(EXIT_FAILURE);
		}

		// Riduco le dimensioni del messaggio ricevuto contenente la risposta del server
		trim_messaggio(messaggio);


		// Se il nome utente inserito non è valido, allora propongo all'utente di re-inserire un nuovo nome
		if (strcmp(messaggio, NOME_NON_VALIDO) == 0)
		{
			printf("\nSpiacente, il nome inserito è già in uso!\nInseriscine un altro: ");

		}
		else
			break;	// Altrimenti il nome dell'utente è valido, possiamo procedere 
	}
	while(1);



	// Definisco il nome da dare al log locale del client
	char nomeFileStandard[] = "_logLocale.txt";

	char nome_user[NAME_MAX_LEN];		// Faccio una copia del nome dell'utente e lo concateno al nome standard dei log locali
	strcpy(nome_user, nome);

	char *nomeFileCompleto = strcat(nome_user, nomeFileStandard);

	// Apro il file logLocale dell'utente client e verifico che sia stato aperto correttamente
	// Il nome del log logale di ogni utente avrà la forma:
	//
	//		nomeUtente_logLocale.txt

	int logFd = open(nomeFileCompleto, O_WRONLY | O_CREAT | O_TRUNC, 0744 );

	if (logFd < 0 ){
		perror("Errore nell'apertura del logLocale: ");
		exit(EXIT_FAILURE);
	}



	// Alloco e riempio la struttura del client
	client = (struct my_client *) malloc(sizeof(struct my_client));

	if (client == NULL){
		perror("Errore nell'allocare la struttura del client:");
		exit(EXIT_FAILURE);
	}

	strcpy(client->nome_utente, nome);
	client-> sockfd = clientFd;
	client-> logLocalFd = logFd;
	client-> chiusura_forzata = 0;		// Imposto a 0 per indicare che è possibile comunicare con il server
	client-> chiudi_client = 0;		// Imposto a 0 per indicare che il client può scambiare messaggi sulla chat

	//NOTA: di impostare l' id del client se ne occupa il server


	
	// Stabilisco che voglio gestire il segnale SIGINT in modo personalizzato
	sigemptyset(&set);
	sigaddset(&set, SIGINT);

	// Riempio la struttura act, dandogli l'insieme dei segnali da gestire e la funzione da usare per gestirli
	act.sa_flags = 0;
	act.sa_mask = set;
	act.sa_handler = &chiudi_client;		// Funzione da me definita per gestire il segnale


	// Per gestire la ricezione del client di un SIGINT: quando riceve un SIGINT invoca la funzione chiudi_client
	int errSig = sigaction(SIGINT, &act, NULL);

	if (errSig < 0){
		perror("Errore nella sigaction: ");
		exit(EXIT_FAILURE);
	}


	printf("=========== BENVENUTO NELLA CHATROOM =============\n\n");

	
	// Definisco e creo tre thread che avranno rispettivamente i compiti di:

	pthread_t thread_invia_messaggi;		//- inviare messaggi al server e aggiornare il log locale

	pthread_t thread_ricevi_messaggi;		//- ricevere messaggi dal server

	pthread_t thread_gestisci_comunicazioni;	//- ricevere le comunicazioni dal server e aggiornare il log locale


	int errInvio = pthread_create(&thread_invia_messaggi, NULL,(void *) gestisci_invio_messaggi, (struct my_client *)client);
	if (errInvio != 0){
		perror("Errore nella creazione del thread per inviare messaggi");
		exit(EXIT_FAILURE);
	}

	int errRicevi = pthread_create(&thread_ricevi_messaggi, NULL, (void *)gestisci_ricezione_messaggi, (struct my_client*)client);
	if (errRicevi != 0){
		perror("Errore nel creare il thread per ricevere messaggi");
		exit(EXIT_FAILURE);
	}

	int errComunicaz = pthread_create(&thread_gestisci_comunicazioni, NULL, (void *)gestisci_comunicazioni, (struct my_client*)client);
	if (errComunicaz != 0){
		perror("Errore nel creare il thread per ricevere le comunicazioni dal server");
		exit(EXIT_FAILURE);
	}

	while(1)
	{
		// Se chiudi_client = 1: il client ha espresso la volontà di uscire dalla chat
		// Se chiusura_forzata = 2: il server è stato chiuso forzatamente e il log locale è stato aggiornato
		
		// Se si verifica una di queste condizioni, allora chiudo il client

		if (client-> chiudi_client == 1 || client-> chiusura_forzata == 2)	
			break;

	}

	printf("\n============= ARRIVEDERCI ============\n");
	
	// Chiudo il socket che ha usato il client per comunicare con il server
	shutdown(client-> sockfd, SHUT_RDWR);

	// Chiudo il fd del log locale
	close(client->logLocalFd);

	// Libero l'area di memoria allocata per la struttura del client
	free(client);

	
	return EXIT_SUCCESS;

}


/*
 * Funzione che usa il thread del client per:
 *
 * - inviare messaggi al server
 * - aggiornare il log locale che riporta ciò che ha fatto l'utente
 *
 * Prende in input un puntatore alla struttura del client, nella quale c'è il sockfd su cui inviare messaggi e il fd del log locale
 */
void gestisci_invio_messaggi(struct my_client *client){

	int checkError;			// Per verificare gli errori delle syscall

	char buffer[MAX_LEN];		// Un buffer di appoggio per i messaggi

	char messaggio[MAX_LEN];	// Per i messaggi scritti dall'utente


	// Scrivo nel buffer un messaggio che indica il momento in cui l'utente si è connesso al server
	sprintf(buffer, "[%s] => INGRESSO NEL SERVER", stampa_orario());
	
	// Riduco le dimensioni del messaggio presente nel buffer e lo formatto per stamparlo nel log locale
	trim_messaggio(buffer);
	strcat(buffer, "\n");

	aggiorna_log(client-> logLocalFd, buffer);
	
	while(1)
	{
		// Se il client ha espresso la volontà di uscire, allora esco dal while
		if (client-> chiudi_client == 1)
			break;

		// Mostro sullo stdout l'ora in cui sto scrivendo
		printf("[%s] tu: ", stampa_orario());

		// Prendo il messaggio dell'utente dallo stdin e ne riduco le dimensioni
		fgets(messaggio, MAX_LEN, stdin);
		trim_messaggio(messaggio);

		// Se il client ha espresso la volontà di uscire:
		if ( strcmp(messaggio, "exit") == 0 || strcmp(messaggio, "EXIT") == 0)
		{

			// Allora lo comunico al server
			sprintf(buffer, "exit");
			checkError = send(client->sockfd, buffer, MAX_LEN, 0);

			if (checkError < 0){
				printf("Errore nell'inviare il messaggio del client %s", client->nome_utente);
				exit(EXIT_FAILURE);
			}

			
			// Formatto il messaggio di uscita per appenderlo al log locale
			sprintf(buffer, "[%s] => USCITA DAL SERVER", stampa_orario());
			
			trim_messaggio(buffer); 

			aggiorna_log(client-> logLocalFd, buffer);


			// Imposto il flag globale a 1 per indicare al processo main che il client va chiuso
			client-> chiudi_client = 1;

			break;
		}

		// Altrimenti, scrivo il messaggio del client sul buffer.
		// Il formato del messaggio è della forma:
		// 	
		// 		[orario] nomeUtente: messaggio

		sprintf(buffer, "[%s] %s: %s\n", stampa_orario(), client->nome_utente, messaggio);
		strcpy(messaggio, buffer);

		// Mando il messaggio nel buffer al server
		checkError = send(client->sockfd, buffer, MAX_LEN, 0);

		if (checkError < 0){
			printf("Errore nell'inviare il messaggio del client %s", client->nome_utente);
			//exit(EXIT_FAILURE);
		}
		else{
			// Altrimenti, formatto il messaggio inviato al server per appenderlo al log locale
			trim_messaggio(messaggio);
			strcat(messaggio, "\n");

			// E lo appendo al log locale
			aggiorna_log(client->logLocalFd, messaggio);
		}

		// Ripulisco l'area del buffer e del messaggio
		bzero(messaggio, MAX_LEN);
		bzero(buffer, MAX_LEN);

		// Ripeto il while per procedere con i prossimi messaggi
	}

	// Termino l'esecuzione del thread
	pthread_exit(0);
}


/*
 * Funzione che usa il thread che si occupa di:
 *
 * - ricevere i messaggi inviati dal server sul sockFd del client
 * - di aggiornare le variabili globali utili al thread che gestisce le comunicazioni del server
 * 
 * Prende in input il puntatore alla struttura del client, contenente il socket per le comunicazioni
 *
 */
void gestisci_ricezione_messaggi(struct my_client *client){
	
	char messaggio[MAX_LEN];	// Per i messaggi ricevuti dal server

	int checkError;			// Per controllare gli errori delle syscall
	
	while(1)
	{
		// Se il client ha espresso la volontà di uscire, allora esco dal while
		if (client-> chiudi_client == 1)
			break;

		// Leggo ciò che è stato scritto dal server sul sockfd
		checkError = recv(client->sockfd, messaggio, MAX_LEN, 0);
		
		if (checkError < 0){
			perror("Errore nel ricevere il messaggio dal server: ");
			//exit(EXIT_FAILURE);
		}
		
		// Il server ha comunicato che la sua chiusura forzata
		else if (strcmp(messaggio, CHIUSURA_SERVER) == 0)
		{

			client-> chiusura_forzata = 1;		// Lo imposto ad 1 cosi il thread per le comunicazioni
								// capisce che va scritta la comunicazione di chiusura forzata sul log locale
			break;

		}
		else if ( checkError > 0 ){

			// Altrimenti, visualizzo sullo stdout il messaggio ricevuto
			printf("\r%s", messaggio);

			// E mostro il nuovo orario in cui potenzialmente l'utente può scrivere
			printf("[%s] tu: ", stampa_orario());
			fflush(stdout);
		}
		
		// Ripulisco l'area di memoria usata per contenere il messaggio ricevuto
		bzero(messaggio, MAX_LEN);

		// Ripeto il while per ricevere i prossimi messaggi
	}

	// Termino l'esecuzione del thread
	pthread_exit(0);

}

/*
 * Funzione che usa il thread che si occupa di:
 * 
 * - aggiornare la variabile globale contenuta nella struttura del client (chiusura_forzata) per forzare l'uscita del client
 *
 * - gestire le comunicazioni che arrivano dal server
 *
 */
void gestisci_comunicazioni(struct my_client* client){
	
	int checkError;		// Per verificare gli errori delle syscall

	while(1)
	{

		if (client-> chiusura_forzata == 1)
		{
			// Se il flag "chiusura_forzata" è 1, allora è arrivata la comunicazione che il server è stato chiuso, pertanto:

			// Stampo sullo stdout la comunicazione della chiusura del server
			printf("%s\n", CHIUSURA_SERVER);

			// Aggiorno il log locale con la comunicazione
			aggiorna_log(client->logLocalFd, CHIUSURA_SERVER);

			// Impongo la chiusura del client aggiornando chiusura_forzata a 2
			client-> chiusura_forzata = 2;		// Quando il flag è a 2, il log locale è stato aggiornato e
								// il processo main può chiudere il client
			break;
	
		}
	}

	// Termino l'esecuzione del thread
	pthread_exit(0);
}



/*
 *  Funzione usata per gestire la ricezione del segnale SIGINT (il quale se non gestito chiuderebbe il client) da parte del client.
 *
 *  Se il client riceve un SIGINT, invece di chiudere il client stesso, gli verrà ricordato che per uscire deve usare gli opportuni comandi
 */
void chiudi_client()
{
	// Mostro la comunicazione (che vedrà solo il client interessato)
	printf("\r\n[COMUNICAZIONE] Per uscire dalla chat: digitare \"exit\" oppure \"EXIT\"\n");

	// Ristampo sullo stdout la possibile ora in cui potrà scrivere
	printf("[%s] tu: ", stampa_orario());

	fflush(stdout);
}


