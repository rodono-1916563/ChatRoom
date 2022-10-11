
#define MAX_LEN 256			// Massima lunghezza di un messaggio

#define MAX_CLIENTS_PENDING 8		// Numero massimo di client che il server terrà appesi

#define NAME_MAX_LEN 20			// Lunghezza massima dei nomi utente


#define NOME_NON_VALIDO "nome non valido"	// Per indicare che il nome del client non è valido

#define NOME_VALIDO "nome valido"		// Per indicare che il nome del client è valido


#define CHIUSURA_SERVER "\n=> COMUNICAZIONE: Il server è stato CHIUSO"		// Comunicazione del server per la chiusura


#define DFL_CLIENTS 2 	// Il numero massimo di client che sa ospitare il server di default. 
			// Tale capacità crescerà in modo dinamico



/*
 * I flag della struttura my_client servono a:
 *
 * ======================
 *
 * chiusura_forzata = 0: il server è aperto ed è possibile comunicare
 *
 * chiusura_forzata = 1: il server ha ricevuto un SIGINT, il thread che gestisce le comunicazioni deve stampare la comuniciazione 
 *
 * chiusura_forzata = 2: il log locale è stato aggiornato, il client può essere chiuso.
 *
 * =======================
 * chiudi_client = 0: il client può scambiare messaggi sulla chat
 *
 * chiudi_client = 1: il client ha espresso esplicitamente la volontà di uscire dalla chat 
 *
 */

struct my_client{

	char nome_utente[NAME_MAX_LEN];	 // Nome dell' utente client
	int sockfd;		 	 // FileDescriptor del socket che definisce
	int id_utente;		 	 // Identificatore univoco del client
	int logLocalFd;		 	 // Il file descriptor del logLocale dell'utente;
	int chiusura_forzata;	 	 // Flag che indica se il server è stato chiuso forzatamente da un SIGINT
	int chiudi_client;	 	 // Flag che indica al processo main la volontà del client di uscire
};


/*
 * I flag della struttura my_server servono a:
 * 
 * ======================
 *
 * server_chiuso = 0: il server è aperto
 *
 * server_chiuso = 1: il server è stato chiuso da un SIGINT
 *
 */
struct my_server{
	
	int id;							// Il primo id possibile che può essere attribuito a un client
	int server_chiuso;					// Flag che se vale 1 indica che il server è stato chiuso
	int logGlobaleFd;					// Fd del file log Globale
				
};



/*
 * Struttura utile a modellare la coda dinamica dei client 
 * connessi al server
 *
 */
struct client_queue{

	struct my_client** queue;				// Puntatore ad un area contenente puntatori a strutture my_client
	int queue_len;						// Lunghezza massima attuale della coda dei client
	int client_connessi;					// Numero di client connessi in un dato istante
};




/*
 * Funzione di utilità che riduce le dimensioni delle stringhe puntate dalle variabili dei buffer o dei messaggi
 *
 */
void trim_messaggio(char *messaggio)
{
	// Riduco la dimensione del messaggio scritto
	for (int i = 0; i < MAX_LEN; i++){
		if (messaggio[i] == '\n'){
			messaggio[i] = '\0';
			break;
		}
	}
}


/*
 * Funzione utile a stampare l'orario attuale di fronte ai messaggi.
 * L'ora è espressa in formato:
 * 	
 * 	[ora:minuti:secondi]
 * 
 * Ritorna il puntatore alla stringa rappresentante l'orario formattato
 */
char *stampa_orario()
{
	time_t oraAttuale;

	time(&oraAttuale);
		
	// Per l'orario in formato esteso
	char *orario = ctime(&oraAttuale);
	
	// Per l'orario in formato [ora:min:sec]
	orario[19] = '\0';
	orario += 11;

	return orario;
}



/*
 * Funzione di utilità per aggiornare i log (sia locali che globali)
 *
 * Prende in input:
 * 
 * - il fd del file su cui scrivere
 * - il puntatore al messaggio da scrivere
 */
void aggiorna_log(int fd, char *buffer)
{
	int errCheck;		// Per controllare se la write() da errore

	//int my_errno = 0;

	do
	{
		errCheck = write(fd, buffer, strlen(buffer));	
		//my_errno = errno;		
		
		if (errCheck < 0){
			perror("Errore nell'aggiornare il log Globale: ");
			
			/*
			if (my_errno != 0)
				//exit(EXIT_FAILURE);
			*/
		}
	}
	while(errCheck < 0);
}

