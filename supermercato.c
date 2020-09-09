#define _POSIX_C_SOURCE  200112L
#define NOMELOG "./finalReport.log"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>

#include "myHeader.h" // funzioni su code e altre utility

static volatile sig_atomic_t sighup=0;
static volatile sig_atomic_t sigquit=0;

static void handler (int signo) {             // handler segnali
	if (signo==SIGQUIT) if (sighup==0){
		sigquit=1;
	}
	if (signo==SIGHUP) if (sigquit==0){
		sighup=1;
	}
}

//Totale prodotti venduti dal negozio
static int totProd=0;
static pthread_mutex_t totProdLock = PTHREAD_MUTEX_INITIALIZER;

//Numero totale di clienti entrati nel supermercato
static int cliTotali=0;
static pthread_mutex_t lockCliTotali = PTHREAD_MUTEX_INITIALIZER;

//Numero di clienti attualmente dentro il supermercato
static int cliAttuali=0;
static pthread_mutex_t lockCliAttuali = PTHREAD_MUTEX_INITIALIZER;

//Numero di clienti usciti
static int clientiUsciti=0;
static pthread_mutex_t lockUsciti = PTHREAD_MUTEX_INITIALIZER;

//Numero di clienti che attende l' ok del direttore per uscire
static int attesaUscita=0;
static pthread_mutex_t lockAttesaUscita = PTHREAD_MUTEX_INITIALIZER;

//Autorizzazione all'uscita (boolean)
static int authUscita=0;
static pthread_mutex_t lockAuthUscita = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condAuthUscita = PTHREAD_COND_INITIALIZER;

static datiCassa *code;	      //Array di casse con code di clienti
static int *lunghezzeCode;		//Array di lunghezze delle code delle casse

//Avverte se il thread ha aggiornato il direttore (boolean)
static int aggDir=0;
static pthread_mutex_t LockAggDir = PTHREAD_MUTEX_INITIALIZER;

//Varibile per inizializzare l'array delle casse prima che qualche clienti provi ad accedere
static int cassaReady=0;
static pthread_mutex_t lockReady = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condReady = PTHREAD_COND_INITIALIZER;

//File su cui verrà prodotto il report finale
static FILE *logfile;
static pthread_mutex_t lockWriteLog = PTHREAD_MUTEX_INITIALIZER;

static config *conf; 		//Struct di configurazioni iniziali

// THREAD COMUNICAZIONI AL DIRETTORE
void *comunica_t (void *arg) {
	if ((lunghezzeCode=malloc(conf->K*sizeof(int))) == NULL) {
		fprintf(stderr, "Errore nella malloc\n");
		exit(EXIT_FAILURE);
	}
	for (int i=0; i<conf->K; ++i) {
		lunghezzeCode[i]=0;
	}
	struct timespec slp = { 0, conf->T*1000000 }; // per evitare di leggere le code prima che arrivino clienti
	nanosleep(&slp, NULL);
	while (sigquit==0) {
		if (sighup==1) {
			pthread_mutex_lock(&lockCliAttuali);
			if (cliAttuali==0) {
				pthread_mutex_unlock(&lockCliAttuali);
				pthread_exit(NULL);
			}
			pthread_mutex_unlock(&lockCliAttuali);
		}
		struct timespec slp = { 0, conf->intervallo*1000000 };
		nanosleep(&slp, NULL);
		pthread_mutex_lock(&LockAggDir);
		for (int i=0; i<conf->K; ++i) {
			lunghezzeCode[i] = getSize(code[i].codaClienti);
		}
		aggDir=1;
		pthread_mutex_unlock(&LockAggDir);
	}
	pthread_exit(NULL);

}

// THREAD CASSA
void *cassa_t (void *arg) {
	int id = *(int*)arg;
	unsigned int seed = id;
	unsigned int tempoFisso = rand_r(&seed) % (60) + 20;
	struct timespec tempoIniziale={0,0};             // ora inizio
	clock_gettime(CLOCK_REALTIME, &tempoIniziale);
	int nProdotti=0, nClienti=0, nChiusure=0;
	float avgTime=0, totTime=0;
	int apertaPrec=0;
	float tempoChiusa=0;
	while (sigquit==0) {
		if (sighup==1) {
			pthread_mutex_lock(&lockCliAttuali);
			if (cliAttuali==0) {
				pthread_mutex_unlock(&lockCliAttuali);
				break;
			} else pthread_mutex_unlock(&lockCliAttuali);
		}
		int exit=0;
		pthread_mutex_lock(&code[id].lockAperta);
		while (code[id].aperta==0 && sighup==0 && sigquit==0) {
			struct timespec t1={0,0};
			clock_gettime(CLOCK_REALTIME, &t1);
			if (apertaPrec==1) {
				nChiusure++;
				apertaPrec=0;
				if (getSize(code[id].codaClienti)>0) {
					deleteCoda(code[id].codaClienti);	//Cosi, se la coda aveva dei clienti ma e' stata chiusa dal direttore_th, elimino la coda di clienti, che si spostano in altre code
					code[id].codaClienti=initCoda();
				}
			}
			pthread_cond_wait(&code[id].condAperta, &code[id].lockAperta);
			struct timespec t2={0,0};
			clock_gettime(CLOCK_REALTIME, &t2);
			tempoChiusa+=(((double)t2.tv_sec + 1.0e-9*t2.tv_nsec)-((double)t1.tv_sec + 1.e-9*t1.tv_nsec));
		}
		apertaPrec=1;
		pthread_mutex_unlock(&code[id].lockAperta);
		if (sigquit==1) {
			if (getSize(code[id].codaClienti)==0) exit=1;
		}
		else if (sighup==1) {
			pthread_mutex_lock(&lockCliAttuali);
			if (cliAttuali==0) exit=1;
			pthread_mutex_unlock(&lockCliAttuali);
		}
		if (exit==1) {
			pthread_cond_broadcast(&code[id].condServito);
			break;
		}
		if (getSize(code[id].codaClienti)>0) {
		  datiCliente *cl;
			cl=pop(code[id].codaClienti);
			nProdotti+=cl->carrello;
			nClienti++;
			struct timespec servtime = { 0, (tempoFisso+(conf->S*cl->carrello))*1000000 }; // tempo in cui serve il cliente
			float serv=((double)servtime.tv_sec + 1.0e-9*servtime.tv_nsec);
			nanosleep(&servtime, NULL);
			totTime+=serv;
			avgTime=(totTime)/nClienti;
			pthread_mutex_lock(&code[id].lockServito);
			*(cl->servito)=1;
			pthread_cond_broadcast(&code[id].condServito);
			pthread_mutex_unlock(&code[id].lockServito);
		}
	}
	struct timespec tempoFinale={0,0};
	clock_gettime(CLOCK_REALTIME, &tempoFinale);  
	float tempoAperturaTot=((double)tempoFinale.tv_sec + 1.0e-9*tempoFinale.tv_nsec)-((double)tempoIniziale.tv_sec + 1.e-9*tempoIniziale.tv_nsec)-tempoChiusa;
	if (tempoAperturaTot<0) tempoAperturaTot=0;
	pthread_mutex_lock(&lockWriteLog);
	fprintf(logfile, "CASSA %d | Prodotti processati: %d | Clienti serviti: %d | Tempo apertura:%.3f s | Tempo medio servizio:%.3f s | n. Chiusure:%d |\n", id, nProdotti, nClienti, tempoAperturaTot, avgTime, nChiusure);
	fflush(logfile);
	pthread_mutex_unlock(&lockWriteLog);
	pthread_exit(NULL);
}

// THREAD CLIENTE
void *cliente_t (void *arg) {
	pthread_mutex_lock(&lockCliAttuali);	//Aumento il numero di clienti all'interno
	cliAttuali++;
	pthread_mutex_unlock(&lockCliAttuali);
	int id=*(int*)arg;
	pthread_mutex_lock(&lockCliTotali);	//Aumento il numero di clienti entrati in totale nel supermercato
	cliTotali++;
	pthread_mutex_unlock(&lockCliTotali);
	struct timespec tempoIniziale={0,0};            // prendo il tempo iniziale
	clock_gettime(CLOCK_REALTIME, &tempoIniziale);
	unsigned int seed = id;
	unsigned int carrello = rand_r(&seed) % (conf->P);
	unsigned int tAcq = rand_r(&seed) % (conf->T-10) + conf->T;  // genero il tempo di acquisti
	struct timespec tAcquisti = { 0, tAcq*1000000 };
	nanosleep(&tAcquisti, NULL);
	if (carrello == 0) {             // uscite senza acquisti
		pthread_mutex_lock(&lockAttesaUscita);
		attesaUscita++;
		pthread_mutex_unlock(&lockAttesaUscita);
		pthread_mutex_lock(&lockAuthUscita);
		while (authUscita==0) {                                // attendo autorizzazione
			pthread_cond_wait(&condAuthUscita, &lockAuthUscita);
		}
		pthread_mutex_unlock(&lockAuthUscita);
		pthread_mutex_lock(&lockAttesaUscita);
		attesaUscita--;
		pthread_mutex_unlock(&lockAttesaUscita);
		pthread_mutex_lock(&lockCliAttuali);	//Diminuisco il numero di clienti all'interno
		cliAttuali--;
		pthread_mutex_unlock(&lockCliAttuali);
		pthread_mutex_lock(&lockUsciti);	//Aumento il numero di clienti usciti
		clientiUsciti++;
		pthread_mutex_unlock(&lockUsciti);
		struct timespec tExit={0,0};
		clock_gettime(CLOCK_REALTIME, &tExit);         // il tempo totale lo calcolo con tempo uscita - tempo entrata
		float tempoTot=((double)tExit.tv_sec + 1.0e-9*tExit.tv_nsec)-((double)tempoIniziale.tv_sec + 1.e-9*tempoIniziale.tv_nsec);
		pthread_mutex_lock(&lockWriteLog);
		fprintf(logfile, "CLIENTE %d | Prodotti acquistati:0 | Tempo nel negozio:%.3f s | Tempo di coda:0 s | Code cambiate:0 |\n", id, tempoTot);
		fflush(logfile);
		pthread_mutex_unlock(&lockWriteLog);
		pthread_exit(NULL);
	}
	pthread_mutex_lock(&totProdLock);	//Aumento il numero di prodotti comprati
	totProd+=carrello;
	pthread_mutex_unlock(&totProdLock);
	int codeCambiate=0; // per contare le code cambiate
	struct timespec inizioFila={0,0};
	clock_gettime(CLOCK_REALTIME, &inizioFila);    // prendo il tempo in cui si mette in fila
	int cassaScelta=0;
	while (sigquit==0) {
		while (1) {
			unsigned int seed=id + rand_r(&seed);
			cassaScelta = rand_r(&seed) % conf->K;       // scelgo randomicamente una cassa
			pthread_mutex_lock(&code[cassaScelta].lockAperta);
			if (code[cassaScelta].aperta==0) {
				pthread_mutex_unlock(&code[cassaScelta].lockAperta);  // se è chiusa reitero
				continue;
			}
			break;
		}
		datiCliente *cl;
		cl=malloc(sizeof(datiCliente));
		if (cl==NULL){
			fprintf(stderr, "Errore nell malloc\n" );
			exit(EXIT_FAILURE);
		}
		cl->id=id;
		cl->servito=(int*)malloc(sizeof(int));
		*(cl->servito)=0;
		cl->carrello=carrello;
		if (push(code[cassaScelta].codaClienti, cl) == -1) {
			fprintf(stderr, "Errore accodamento cliente %d\n", id);
			free(cl->servito);
			free(cl);
			pthread_exit(NULL);
		}
		pthread_mutex_unlock(&code[cassaScelta].lockAperta);
		codeCambiate++;
		if (sigquit==0) {
			pthread_mutex_lock(&code[cassaScelta].lockServito);
			int riaccoda=0, s_quit=0;
			while (*(cl->servito) == 0) {
				if (sigquit==1) {
					s_quit=1;
					break;
				}
				pthread_mutex_lock(&code[cassaScelta].lockAperta);  // se chiude la cassa mi devo riaccodare in un'altra
				if (code[cassaScelta].aperta==0) {
					riaccoda=1;
				}
				pthread_mutex_unlock(&code[cassaScelta].lockAperta);
				if (riaccoda==0) {
					pthread_cond_wait(&code[cassaScelta].condServito, &code[cassaScelta].lockServito);
				}
				else break;
			}
			pthread_mutex_unlock(&code[cassaScelta].lockServito);
			free(cl->servito);
			free(cl);
			if (riaccoda==0 || s_quit==1) {
				break;
			}
		}
		else {
			free(cl->servito);
			free(cl);
			break;
		}
	}
	pthread_mutex_lock(&lockCliAttuali);
	cliAttuali--;
	pthread_mutex_unlock(&lockCliAttuali);
	pthread_mutex_lock(&lockUsciti);
	clientiUsciti++;
	pthread_mutex_unlock(&lockUsciti);
	//Calcolo tempi di fine
	struct timespec tExit={0,0};
	clock_gettime(CLOCK_REALTIME, &tExit);  // prendo il tempo finale e calcolo i tempi di permanenza in negozio e di fila
	float tempoTot=((double)tExit.tv_sec + 1.0e-9*tExit.tv_nsec)-((double)tempoIniziale.tv_sec + 1.e-9*tempoIniziale.tv_nsec);
	float tempoFila=((double)tExit.tv_sec + 1.0e-9*tExit.tv_nsec)-((double)inizioFila.tv_sec + 1.e-9*inizioFila.tv_nsec);
	//Scrivo i dati nel logfile
	pthread_mutex_lock(&lockWriteLog);
	fprintf(logfile, "CLIENTE %d | Prodotti acquistati:%d | Tempo nel negozio:%0.3f s | Tempo in coda:%0.3f s | Code cambiate:%d |\n", id, carrello, tempoTot, tempoFila, codeCambiate); fflush(logfile);
	pthread_mutex_unlock(&lockWriteLog);
	pthread_exit(NULL);
}

// THREAD GESTIONE CASSE
void *gestNegozio_th (void *arg) {
	int nCasseIniziali= conf->casseInit;
	pthread_t *casse;                          // casse viste come un array di thread
	if ((casse=malloc(conf->K*sizeof(pthread_t))) == NULL) {
		fprintf(stderr, "Errore nella malloc\n");
		exit(EXIT_FAILURE);
	}
	if ((code=malloc(conf->K*sizeof(datiCassa))) == NULL) {
		fprintf(stderr,"Errore nella malloc\n");
		exit(EXIT_FAILURE);
	}
	for (int i=0; i<conf->K; ++i) {
		code[i].id=i;  // i numeri cassa partono da 0
		code[i].aperta=0;
		code[i].codaClienti=initCoda();
		if (pthread_cond_init(&code[i].condServito, NULL) != 0) {
			fprintf(stderr, "Errore inizializzazione var cond servizio cassa %d\n", i);
			exit(EXIT_FAILURE);
		}
		if (pthread_mutex_init(&code[i].lockServito, NULL) != 0) {
			fprintf(stderr, "Errore inizializzazione lock servizio cassa %d\n", i);
			exit(EXIT_FAILURE);
		}
		if (pthread_mutex_init(&code[i].lockAperta, NULL) != 0) {
			fprintf(stderr, "Errore inizializzazione lock apertura coda %d\n", i);
			exit(EXIT_FAILURE);
		}
		if (pthread_cond_init(&code[i].condAperta, NULL) != 0) {
			fprintf(stderr, "Erroe inizializzazione var cond apertura cassa %d\n", i);
			exit(EXIT_FAILURE);
		}
	}
	int *idC;  // numero di cassa
	idC=malloc(conf->K*sizeof(int));
	if (idC==NULL){
		fprintf(stderr, "Errore nella malloc\n");
		exit(EXIT_FAILURE);
	}
	for (int i=0; i<conf->K; ++i) {
		idC[i]=i;
	}
	for (int i=0; i<conf->K; ++i) {
		if (pthread_create(&casse[i], NULL, cassa_t, &idC[i]) != 0) {
				fprintf(stderr, "Error creazione thread cassa %d\n", i);
				exit(EXIT_FAILURE);
		}
		if (i<nCasseIniziali) {   // apro le casse richieste dal config
			pthread_mutex_lock(&code[i].lockAperta);
			code[i].aperta=1;
			pthread_cond_signal(&code[i].condAperta);
			pthread_mutex_unlock(&code[i].lockAperta);
		}
	}
	pthread_mutex_lock(&lockReady);
	cassaReady=1;
	pthread_cond_signal(&condReady);
	pthread_mutex_unlock(&lockReady);
	int nCasseAperte= nCasseIniziali;
	while (sighup==0 && sigquit==0) {
		pthread_mutex_lock(&LockAggDir);
		if (aggDir==1) {
			int s1=0, s2=0;
			for (int i=0;i<conf->K;i++) {      // controllo dei parametri di soglia s1 e s2
				if (lunghezzeCode[i]<=1) {
					s1++;
				}
				else if (lunghezzeCode[i] >= conf->S2) {
					s2=1;
				}
			}
			if (!(s1 >= conf->S1 && s2==1)) {
				if (s1>=conf->S1) {
					if (nCasseAperte>1) {       // controllo se posso chiudere la cassa (se è l'unica aperta no)
						for (int i=0; i<conf->K; ++i) {
							pthread_mutex_lock(&code[i].lockAperta);
							if (code[i].aperta == 1) {
								code[i].aperta=0;
								nCasseAperte--;
								pthread_mutex_unlock(&code[i].lockAperta);
								break;
							}
							pthread_mutex_unlock(&code[i].lockAperta);
						}
					}
				}
				else if (s2==1) {
					if (nCasseAperte < conf->K) {          // controlo se posso aprirla, potrebbero essere tutte e k operative
						for (int i=0; i<conf->K; ++i) {
							pthread_mutex_lock(&code[i].lockAperta);
							if (code[i].aperta == 0) {
								code[i].aperta=1;
								pthread_cond_signal(&code[i].condAperta);
								nCasseAperte++;
								pthread_mutex_unlock(&code[i].lockAperta);
								break;
							}
							pthread_mutex_unlock(&code[i].lockAperta);
						}
					}
				}
			}
			aggDir=0;    // rimetto a 0 la notifica
		}
		pthread_mutex_unlock(&LockAggDir);
	}
	if (sigquit==1) {
		for (int i=0; i<conf->K; ++i) {
			pthread_cond_signal(&code[i].condAperta);
		}
	}
	else if (sighup==1) {
		while (1) {
			pthread_mutex_lock(&lockCliAttuali);
			if (cliAttuali==0) {
				for (int i=0; i<conf->K; ++i) {
					pthread_cond_signal(&code[i].condAperta);
				}
				pthread_mutex_unlock(&lockCliAttuali);
				break;
			}
			else pthread_mutex_unlock(&lockCliAttuali);
		}
	}
	for (int i=0; i<conf->K; ++i) {
		if (pthread_join(casse[i], NULL) != 0) {
			fprintf(stderr, "Errore join thread cassa %d\n", i);
			exit(EXIT_FAILURE);
		}
	}
	free(casse);
	free(idC);
	pthread_exit(NULL);
}

// THREAD GESTIONE IN-OUT CLIENTI
void *gestClienti_th (void *arg) {
	int cliDentro = conf->C;
	pthread_t *clienti;         // i thread clienti lo organizzo come array
	if ((clienti=(pthread_t*)malloc(conf->C*sizeof(pthread_t))) == NULL) {
		fprintf(stderr, "Errore nella malloc\n");
		exit(EXIT_FAILURE);
	}
	pthread_mutex_lock(&lockReady);
	while (cassaReady==0) {
		pthread_cond_wait(&condReady, &lockReady);
	}
	pthread_mutex_unlock(&lockReady);
	int *idCli;
	idCli= malloc(conf->C*sizeof(int));
	if (idCli==NULL){
		fprintf(stderr, "Errore nella malloc\n");
		exit(EXIT_FAILURE);
	}
	for (int j=0; j<conf->C; ++j) {
		idCli[j]=j;
	}
	int i=0;
	for (i=0; i<conf->C; i++) {
		if (pthread_create(&clienti[i], NULL, cliente_t, &idCli[i]) != 0) {
			fprintf(stderr, "Errore creazione thread cliente\n");
			exit(EXIT_FAILURE);
		}
	}
	pthread_t comunica;
	if (pthread_create(&comunica, NULL, comunica_t, NULL) != 0) {
		fprintf(stderr, "Errore creazione thread comunicazioni direttore_th\n");
		exit(EXIT_FAILURE);
	}
	while (sigquit==0 && sighup==0) {
		pthread_mutex_lock(&lockAuthUscita);
		authUscita=0;
		pthread_mutex_unlock(&lockAuthUscita);
		pthread_mutex_lock(&lockUsciti);
		if (clientiUsciti >= conf->E) {             // controllo clienti in negozio
			cliDentro+=conf->E;
			clienti=(pthread_t*)realloc(clienti, cliDentro*sizeof(pthread_t));  // se raggiungo E ne rialloco altri E
			idCli=(int*)realloc(idCli, cliDentro*sizeof(int));
			int ind=i;
			for (int j=0; j<conf->E; j++) {
				idCli[ind]=ind;
				ind++;
			}
			for (int j=0; j<conf->E; j++) {
				if (pthread_create(&clienti[i], NULL, cliente_t, &idCli[i]) != 0) {      // creo nuovi clienti
					fprintf(stderr, "Errore creazione thread clienti\n");
					exit(EXIT_FAILURE);
				}
				i++;
			}
			clientiUsciti=0;    // riazzero le uscite
		}
		pthread_mutex_unlock(&lockUsciti);
		pthread_mutex_lock(&lockAttesaUscita);
		if (attesaUscita > 0) {                // faccio uscire i clienti senza acquisti
			pthread_mutex_lock(&lockAuthUscita);
			authUscita=1;
			pthread_cond_broadcast(&condAuthUscita);
			pthread_mutex_unlock(&lockAuthUscita);
		}
		pthread_mutex_unlock(&lockAttesaUscita);
	}
	pthread_mutex_lock(&lockAuthUscita);
	authUscita=1;
	pthread_cond_broadcast(&condAuthUscita);
	pthread_mutex_unlock(&lockAuthUscita);
	for (int i=0; i<cliDentro; ++i) {
		if (pthread_join(clienti[i], NULL) != 0) {
			fprintf(stderr, "Errore join clienti\n");
			exit(EXIT_FAILURE);
		}
	}
	if (pthread_join(comunica, NULL) != 0) {
		fprintf(stderr, "Errore join comunicazioni\n");
		exit(EXIT_FAILURE);
	}
	free(clienti);
	free(idCli);
	pthread_exit(NULL);
}

//THREAD DIRETTORE GENERALE
void *direttore_th (void *arg) {
	pthread_t market;
	if (pthread_create(&market, NULL, gestNegozio_th, NULL) != 0) {
		fprintf(stderr, "Errore creazione thread gestione negozio\n" );
		exit(EXIT_FAILURE);
	}
	pthread_t gestCli;
	if (pthread_create(&gestCli, NULL, gestClienti_th, NULL) != 0) {    // avvia i suoi subthread e li attende
		fprintf(stderr, "Errore creazione thread gestione clienti\n" );
		exit(EXIT_FAILURE);
	}
	if (pthread_join(market, NULL) != 0) {
		fprintf(stderr, "Errore join gestione negozio\n" );
		exit(EXIT_FAILURE);
	}
	if (pthread_join(gestCli, NULL) != 0) {
		fprintf(stderr, "Errore join gestione clienti\n" );
		exit(EXIT_FAILURE);
	}
	pthread_exit(NULL);
}

int main (int argc, char*argv[]) {
	if ((conf=leggiConf("./test/config.txt")) == NULL ) {
		fprintf(stderr, "Problema nell'acquisizione dei parametri iniziali\n");
		exit(EXIT_FAILURE);
	}
	struct sigaction signals;
	memset(&signals, 0, sizeof(signals));
	signals.sa_handler=handler;
	if (sigaction(SIGHUP,&signals,NULL)==-1) {
    fprintf(stderr,"Errore nell'handler del SIGHUP\n");
  }
  if (sigaction(SIGQUIT,&signals,NULL)==-1) {
    fprintf(stderr,"Errore nell'handler del SIGQUIT\n");
  }
	if ((logfile=fopen(NOMELOG, "w")) == NULL) {
		fprintf(stderr, "Problema apertura file di log\n");
		exit(EXIT_FAILURE);
	}
	pthread_t direttore;
	if (pthread_create(&direttore, NULL, direttore_th, NULL) != 0) {
		fprintf(stderr, "Errore creazione thread direttore\n" );
		exit(EXIT_FAILURE);
	}
	if (pthread_join(direttore, NULL) != 0) {
		fprintf(stderr, "Errore join thread direttore\n" );
		exit(EXIT_FAILURE);
	}
	fprintf(logfile, "\nCLIENTI SERVITI: %d\nPRODOTTI VENDUTI: %d\n", cliTotali, totProd); // riepilogo
	fflush(logfile);
	fclose(logfile);
	for (int i=0; i<conf->K; ++i) {
		deleteCoda(code[i].codaClienti);
		pthread_mutex_destroy(&code[i].lockAperta);
		pthread_cond_destroy(&code[i].condAperta);
		pthread_mutex_destroy(&code[i].lockServito);
		pthread_cond_destroy(&code[i].condServito);
	}
	free(code);
	free(lunghezzeCode);
	free(conf);
	printf("### SUPERMERCATO CHIUSO ###\n");
	return 0;
}
