#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <pthread.h>
#include <signal.h>

#include "strutture.h"		// Header file con le strutture e funzioni comuni da me definite


// Variabili globali

struct my_server *server;		 	// Puntatore alla struttura del server

struct client_queue *coda;			// Puntatore alla struttura della coda dei client connessi

pthread_mutex_t client_queue_lock;		// Mutex per l'accesso alla coda dei client


// Dichiarazione delle funzioni usate
void gestisci_client(struct my_client *client);

void remove_from_queue(int id_utente);

void add_to_queue(struct my_client *client);

void send_all_except(char *buffer, int id_utente_sender);

void chiudi_server();

int controlla_validita_nome(char* nome);


/*
 * Main per la gestione del server
 */
int main(int argc, char * args[])	// In args[1] ci sta la porta TCP
{
	
	int port_num, serverFd, comunication_fd, errCheck;

	struct sockaddr_in server_addr, client_addr;

	// Definisco il puntatore alla struttura per il client
	struct my_client *client;

	// Definisco la struttura che mi servirà per gestire i segnali
	struct sigaction act;
	sigset_t set;

	
	// Controllo che il numero di parametri con cui è stato lanciato il programma sia corretto
	if (argc != 2)
	{
		printf("Errore! Ci si aspetta: comando < porta > !\n");
		exit(EXIT_FAILURE);
	}


	// Definisco la unnamed socket del server e verifico che sia andato tutto correttamente
	serverFd = socket(AF_INET, SOCK_STREAM, 0);
	if (serverFd < 0)
	{
		perror("Errore nella definizione della socket: ");
		exit(EXIT_FAILURE);
	}
	
	// Converto in intero il numero di porta passato come parametro
	port_num = atoi(args[1]);

	/* Preparo la struttura server_addr */
	server_addr.sin_family = AF_INET;			// Per usare l'IPV4
	server_addr.sin_addr.s_addr = inet_addr("0.0.0.0");	// Per poter ricevere richieste di connessione da chiunque
	server_addr.sin_port = htons(port_num);			// Per impostare il numero di porta in formato network


	// Binding
	if (bind(serverFd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
	{
		perror("Errore durante il binding: ");
		exit(EXIT_FAILURE);
	}
	
	// Listen
	if (listen(serverFd, MAX_CLIENTS_PENDING) < 0)
	{
		perror("Errore durante la listen: ");
		exit(EXIT_FAILURE);
	}


	// Apro/Creo il file che farà da log Globale, e verifico che venga aperto correttamente
	int logGlobale_fd = open("logGlobale.txt", O_WRONLY | O_CREAT | O_TRUNC, 0744);
	
	if (logGlobale_fd < 0 ){
		perror("Errore nell'apertura del log globale");
		exit(EXIT_FAILURE);
	}


	// Stabilisco che voglio gestire il segnale SIGINT
	sigemptyset(&set);
	sigaddset(&set, SIGINT);

	// Riempio la struttura act, dandogli l'insieme dei segnali da gestire e la funzione da usare per gestirli
	act.sa_flags = 0;
	act.sa_mask = set;
	act.sa_handler = &chiudi_server;	// Funzione da me definita per chiudere il server


	// Meccanismo per chiudere il server: quando riceve un SIGINT chiude il server
	sigaction(SIGINT, &act, NULL);


	// Alloco l'area di memoria che conterrà la struttura globale del server
	server = (struct my_server *) malloc(sizeof(struct my_server));

	if (server == NULL){
		perror("Errore nell'allocare la struttura del server:");
		exit(EXIT_FAILURE);
	}

	// Inizializzo le informazioni del server
	server-> id = 0;				// Primo id disponibile da assegnare ai client
	server-> logGlobaleFd = logGlobale_fd;		// Setto il fd del file usato per il logGlobale
	server-> server_chiuso = 0;			// Vuol dire che il server è aperto



	// Alloco l'area di memoria che contiene informazioni sulla coda dei client e verifico che sia stata allocata correttamente
	coda = (struct client_queue*) malloc (sizeof(struct client_queue));

	if (coda == NULL){
		perror("Errore nell'allocare la struttura contenente info sulla coda dei client:");
		exit(EXIT_FAILURE);
	}

	// Alloco l'area di memoria che conterrà i puntatori ai client connessi al server
	coda-> queue = (struct my_client**) malloc (DFL_CLIENTS * sizeof(struct my_client*));

	if (coda-> queue == NULL){
		perror("Errore nell'allocare la coda dei puntatori dei client:");
		exit(EXIT_FAILURE);
	}

	// Setto tutta l'area allocata per la coda dei puntatori al valore NULL
	for (int i = 0; i < DFL_CLIENTS; i++)
		coda-> queue[i] = NULL;
	

	// Inizializzo le informazioni sulla coda dei client
	coda-> client_connessi = 0;
	coda-> queue_len = DFL_CLIENTS;		// Imposto la lunghezza max della coda a un valore di default				
	
	
	
	// Inizializzo un mutex per la coda dei client
	pthread_mutex_init(&client_queue_lock, NULL);


	char buffer[] = "\n============== IL SERVER E' APERTO ==========\n";
	printf("%s\n", buffer);
	

	// Scrivo sul log globale che il server è stato aperto
	aggiorna_log(server-> logGlobaleFd, strcat(buffer, "\n"));


	while(1)
	{	

		// Accetto la richiesta di connessione del client
		socklen_t client_len = sizeof(client_addr);
		comunication_fd = accept(serverFd, (struct sockaddr *)&client_addr, &client_len);


		// Se il server ha ricevuto un SIGINT, ossia è stato chiuso 
		// (con CTRL + C ad esempio) allora esci dal while
		if (comunication_fd < 0 && server-> server_chiuso == 1)		// La condizione comunication_fd è necessaria perchè il SIGINT 
										// blocca la accept producendo errore. 
				break;						// Tuttavia, poichè il server è stato chiuso dal SIGINT stesso
										// possiamo ignorarlo e chiudere il server

		
		// Se c'è stato un errore durante l'accept() mostra l'errore e procedi
		// con la prossima richiesta di connessione
		else if (comunication_fd < 0)
		{
			perror("Errore nell'accettare la comunicazione con il client: ");
			continue;
		}
		

		// Se nessuna delle precedenti condizioni è verificata, tutto è sotto controllo


		// Procedo allocando l'area di memoria per la struttura del client accettato
		client = (struct my_client*) malloc(sizeof(struct my_client));

		if (client == NULL){
			perror("Errore nell'allocare la struttura del client:");
			exit(EXIT_FAILURE);
		}

		// Inizializzo le informazioni del client
		client->sockfd = comunication_fd;		// Imposto il fd sul quale avverrà la comunicazione
		client->id_utente = server-> id++;		// Imposto il primo id disponibile


		// Aggiungo il puntatore al client alla coda
		add_to_queue(client);

		// Definisco e creo il thread gestore del client
		pthread_t thread_gestore_client;

		pthread_create(&thread_gestore_client, NULL, (void *)gestisci_client, (struct my_client*)client);


		// Ripeto il ciclo While per attendere e accettare prossime richieste di connessione

	}

	
	// Stampo sullo stdout che il server è chiuso
	sprintf(buffer, "\n\n============ IL SERVER E' CHIUSO  ============\n");
	printf("%s", buffer);

	// Scrivo sul log globale che il server è chiuso
	aggiorna_log(server-> logGlobaleFd, buffer);

	// Dealloco il mutex precedentemente creato per la coda dei client
	pthread_mutex_destroy(&client_queue_lock);


	// Chiudo il fd della socket
	shutdown(serverFd, SHUT_RDWR);
	
	// Libero la memoria occupata dalla struttura del server
	free(server);

	// Libero la memoria occupata per la coda dei puntatori ai client 
	free((coda->queue));

	// Libero la memoria allocata per la struttura con le informazioni sulla coda
	free(coda);
	
	// NOTA: l'area di memoria allocata per ogni client viene liberata dal thread che gestisce il client nel momento della sua chiusura


	// Chiudo il fd del log Globale
	close(server-> logGlobaleFd);

	return EXIT_SUCCESS;
}


/*
 * Funzione di start che usano i thread del server:
 * 
 * - per gestire le comunicazioni con il client 
 * - aggiornare il log globale del server
 * - deallocare l'area di memoria occupata dal client che gestiscono
 *
 * Prende in input:
 * - il puntatore alla struttura my_client contenente informazioni riguardo il client che si vuole gestire
 */
void gestisci_client(struct my_client *client){

	int n;					// Per il numero di byte ricevuti dal server

	int errCheck;				// Per verificare gli errori delle syscall

	char buffer[MAX_LEN];			// Buffer di appoggio per i messaggi ricevuti dal client

	char nome[NAME_MAX_LEN];		// Per il nome del client da gestire
	
	int esito;				// Per controllare se il nome dell'utente è valido



	// Questo do-While serve ad assicurarsi che nel server sia presente al massimo una e una sola persona con un certo nome
	// E' un controllo che viene fatto ogni volta che un client cerca di connettersi alla chat
	do 
	{
		// Ricevo il nome del client e verifico che sia stato ricevuto con successo
		errCheck = recv(client-> sockfd, nome, NAME_MAX_LEN, 0);

		if (errCheck < 0)
		{
			perror("Errore nel ricevere il nome del client: ");
			//exit(EXIT_FAILURE);
			goto chiudi_thread;	// Se ci sono stati errori, salto a liberare l'area occupata dal client e a chiudere i fd
		}
		else if(strcmp(nome, "exit") == 0 || strcmp(nome, "EXIT") == 0)
		{
			goto chiudi_thread;	// Se vuole uscire, salto a liberare l'area occupata dal client e a chiudere i fd	
		}

		// Riduco le dimensioni del  nome
		trim_messaggio(nome);		

		// Controllo se il nome è valido
		esito = controlla_validita_nome(nome);

		// Se il nome non è valido, allora:
		if (esito == 0 )
		{	
			// Comunico al client che il nome non è valido
			errCheck = send(client->sockfd, NOME_NON_VALIDO, strlen(NOME_NON_VALIDO), 0);

			if ( errCheck < 0 ){
				perror("Errore nell'inviare la comunicazione del nome non valido: ");
				exit(EXIT_FAILURE);
			}
		}
		else
		{
			// Altrimenti il nome è valido. Posso mandare al client la notifica che è valido		
			errCheck = send(client->sockfd, NOME_VALIDO, strlen(NOME_VALIDO), 0);

			if ( errCheck < 0 ){
				perror("Errore nell'inviare la comunicazione che il nome è valido al client: ");
				exit(EXIT_FAILURE);
			}

			// A questo punto posso mettere il nome ricevuto dal client nel campo apposito della struttura dedicata al client
			strcpy(client-> nome_utente, nome);

			break;
		}
	}
	while(1);
	

	//Scrivo nel buffer che il client è entrato nella chat, e lo stampo sullo stdout
	sprintf(buffer, "[%s] => %s è entrato nella chat\n", stampa_orario(), client->nome_utente);
	printf("%s", buffer);


	// Scrivo sul log globale la comunicazione dell'entrata dell'utente
	aggiorna_log(server-> logGlobaleFd, buffer);


	// Mando la comunicazione dell'ingresso del client a tutti gli altri client connessi al server (eccetto al client stesso che 
	// è appena entrato.
	send_all_except(buffer, client->id_utente);


	while(1)
	{
		
		// Leggo ciò che ha scritto il client e lo metto nel buffer
		n = recv(client->sockfd, buffer, MAX_LEN, 0);

		if (n < 0)
		{
			// Se c'è stato un errore, lo riporto e continuo il while per i prossimi messaggi del client
			perror("Errore nel ricevere il messaggio del client: ");

			//exit(EXIT_FAILURE);
			continue;
		}
		
		else if (strcmp(buffer, "exit") == 0 || strcmp(buffer, "EXIT") == 0)
		{

			// Se l' utente ha dichiarato di voler uscire, allora:
			// - lo stampo sullo stdout
			// - mando la comunicazione a tutti gli altri utenti (escludendo il client che sta uscendo)

			sprintf(buffer, "[%s] => %s ha abbandonato la chat\n", stampa_orario(), client->nome_utente);
			printf("%s", buffer);

			send_all_except(buffer, client->id_utente);


			// Scrivo sul log globale che l'utente ha abbandonato la chat
			aggiorna_log(server-> logGlobaleFd, buffer);


			// Esco dal while
			break;

		}
		else
		{
			// Altrimenti Ho che: n  > 0 , pertanto:

			// Mando il messaggio del client a tutti gli altri client connessi (eccetto a quello che ha inviato il messaggio stesso)
			send_all_except(buffer, client->id_utente);

			// Lo stampo sullo stdout
			printf("%s", buffer);

			// Scrivo il messaggio del client sul log globale
			aggiorna_log(server-> logGlobaleFd, buffer);

		}
		
		// Ripulisco il buffer
		bzero(buffer, MAX_LEN);
	}

	chiudi_thread:	// Etichetta raggiunta dal goto, nel caso in cui ci siano stati errori nel ricevere il nome del client

	// Chiudo la comunicazione con il client e lo rimuovo dalla coda dei client connessi al server
	close(client->sockfd);
	remove_from_queue(client->id_utente);
	
	// Libero l'area di memoria allocata per il client
	free(client);

	// Termino il thread
	pthread_exit(0);
}


/*
 * Funzione che dato l'id univoco di un client, lo rimuove dalla coda dei client connessi al server
 *
 */
void remove_from_queue(int id_utente){
	
	// Inizio una sezione critica per impedire che due thread accedano contemporaneamente a stesse posizioni
	// della coda dei client
	pthread_mutex_lock(&client_queue_lock);
	
	for (int i = 0; i < coda-> queue_len; i++)
	{
		if (coda-> queue[i]->id_utente == id_utente)
		{
			coda-> queue[i] = NULL;		// Lo pongo a NULL per poter riusare questa locazione in seguito
			
			coda-> client_connessi--;	// Decremento il numero di client connessi
			break;
		}
	}

	// Chiudo la sezione critica
	pthread_mutex_unlock(&client_queue_lock);

}


/*
 * Funzione che dato un puntatore alla struttura di un client, lo aggiunge nella coda dei client nel primo spazio libero
 *
 */
void add_to_queue(struct my_client *client){
	
	// Inizio una sezione critica per impedire che due thread 
	// provino nello stesso momento a mettere due client diversi nella stessa posizione della coda
	pthread_mutex_lock(&client_queue_lock);
	
	
	// Se non c'è abbastanza spazio per aggiungere nuovi client, allora rialloco l'area adibita per la coda ad un area grande il doppio
	if (coda-> client_connessi >= coda-> queue_len)	
	{
		int len = coda-> queue_len;

		// Rialloco l'area
		coda->queue = (struct my_client**) realloc (coda->queue, 2 * len * sizeof(struct my_client*));

		if ( coda-> queue == NULL){ 
			perror("Errore nel riallocare la coda dei client: "); 
			exit(EXIT_FAILURE);
		}
	

		// Inizializzo la nuova area di memoria allocata con il valore NULL
		for (int i = len; i < 2 * len; i++)
			coda-> queue[i] = NULL;
		

		// Aggiorno il campo che mantiene la lunghezza massima attuale della coda
		coda-> queue_len *= 2;


		printf("\n[Comunicazione] Realloc riuscita\n\n");		// Test usato per verificare quando viene fatta la realloc. 
										// Di default è commentato per non comparire nella chat
	}

	
	// Sia che nella coda ci fosse già spazio per il client, sia che la coda è stata riallocata, 
	// aggiungo il puntatore al client nella prima posizione libera
	for (int i = 0; i < coda-> queue_len ; i++)
	{
		if (coda-> queue[i] == NULL){
			coda-> queue[i] = client;		// Imposto il puntatore al client

			coda-> client_connessi++;		// Incremento il numero di client connessi
			break;
		}
	}

	// Chiudo la sezione critica
	pthread_mutex_unlock(&client_queue_lock);

}


/*
 * Funzione per mandare il messaggio sul buffer a tutti i client connessi al server, ad eccezione di colui che ha inviato il messaggio
 *
 * Prende in input:
 * - il puntatore al buffer con il messaggio
 * - l'id dell'utente che ha mandato il messaggio, (ossia colui che non dovrà riceverlo)
 *
 */
void send_all_except(char *buffer, int id_utente_sender)
{

	int errCheck; 	
	
	for (int i = 0; i < coda->queue_len; i++)
	{
		
		// Se il client non è colui che ha inviato il messaggio, allora può riceverlo
		if (coda-> queue[i] != NULL && coda-> queue[i]->id_utente != id_utente_sender)
		{
			errCheck = send(coda-> queue[i]->sockfd, buffer, MAX_LEN, 0);

			if ( errCheck < 0 ){
				perror("Errore nell'inviare il messaggio a tutti i client connessi: ");
				exit(EXIT_FAILURE);
			}
		}
	}


}

/*
 * Funzione dell'handler che si occupa della chiusura del server.
 *
 * Se al server arriva un SIGINT, allora viene invocata.
 *
 */
void chiudi_server(){

	int errCheck;		// Per verificare gli errori delle syscall


	// Stampo sullo stdout il messaggio della chiusura del server
	printf("%s", CHIUSURA_SERVER);


	// Scrivo sul log globale la comunicazione della chiusura del server
	aggiorna_log(server-> logGlobaleFd, CHIUSURA_SERVER);



	// Invio a tutti i client connessi la comunicazione della chiusura del Server
	for (int i = 0; i < coda->queue_len; i++)
	{
		
		if (coda-> queue[i] != NULL)
		{
			errCheck = send(coda-> queue[i]->sockfd, CHIUSURA_SERVER, strlen(CHIUSURA_SERVER), 0);

			if ( errCheck < 0 ){
				perror("Errore nell'inviare la comunicazione della chiusura a tutti i client connessi: ");
				exit(EXIT_FAILURE);
			}

			// Libero l'area di memoria allocata dai client e decremento il numero dei client connessi
			free(coda-> queue[i]);
			coda->client_connessi--;
		}
	}	


	// Imposto il flag globale del server a 1 per indicare che è stato ricevuto un SIGINT. In questo modo,
	// il processo main può procedere con la chiusura del server
	server-> server_chiuso = 1;

}




/*
 *  Funzione di utilità che dato un puntatore a un nome_utente, verifica se esiste già un altro client che ha un
 *  nome utente uguale.
 *
 *  Ritorna:
 * 
 * - zero 0: se esiste già un altro client connesso con lo stesso nome
 * - uno 1 : se non esiste. Ossia se il nome è valido 
 */
int controlla_validita_nome(char* nome)
{

	for (int i = 0; i < coda->queue_len; i++)
	{

		// Se esiste già un client che ha nome_utente == nome allora:
		if (coda-> queue[i] != NULL && strcmp(coda-> queue[i]-> nome_utente, nome) == 0)	
			return 0;	// Ritorno 0, per indicare che è un insuccesso
	}

	// Altrimenti, non esiste nessun client già connesso al server che ha già usato il nome
	// Restituisco 1 per dire che l'esito è un succeso
	return 1;
}

