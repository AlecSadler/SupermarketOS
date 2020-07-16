// FUNZIONI E STRUCT DI APPOGGIO AL PROGETTO

#define BUFFLEN 256

// STRUCT PER LA CONFIGURAZIONE INIZIALE
typedef struct config {
	int K;
	int C;
	int E;
	int T;
	int P;
	int S;  // S non è usato nella versione semplificata, l'ho trasformato nel tempo impiegato dalla cassa per un prodotto
	int S1;
	int S2;
	int casseInit;
	int intervallo;
} config;

// STAMPA LA CONFIGURAZIONE DI APERTURA
void printconf(config c) {
	printf("#### CONFIGURAZIONE DI APERTURA ####\n");
	printf("K=%d\nC=%d\nE=%d\nT=%d\nP=%d\nS=%d\nS1=%d\nS2=%d\nCASSE APERTE INIZIALI=%d\nINTERVALLO NOTIFICHE=%d\n", c.K, c.C, c.E, c.T, c.P, c.S, c.S1, c.S2, c.casseInit, c.intervallo);
}

// ACQUISISCE I PARAMETRI DAL FILE CONFIGURAZIONE
config *leggiConf (char* configfile) {
	FILE *fd=NULL;
	config *conf;
	char *buffer;
	if ((fd=fopen(configfile, "r")) == NULL) {
		fclose(fd);
		fprintf(stderr, "Errore apertura file di configurazione\n");
		return NULL;
	}
	if ((conf=malloc(sizeof(config))) == NULL) {
		fclose(fd);
		free(conf);
		fprintf(stderr, "Errore nella malloc\n");
		return NULL;
	}
	if ((buffer=malloc(BUFFLEN*sizeof(char))) == NULL) {
		fclose(fd);
		free(conf);
		free(buffer);
		fprintf(stderr, "Errore nella malloc\n");
		return NULL;
	}
	int line=0;
	char* tmp;
	while (fgets(buffer, BUFFLEN, fd) != NULL) {
		tmp= buffer;
		while (*buffer != '=') {
			buffer++;
		}
		buffer++;
		switch (line) {
			case 0:
				conf->K=atoi(buffer);
				break;
			case 1:
				conf->C=atoi(buffer);
				break;
			case 2:
				conf->E=atoi(buffer);
				break;
			case 3:
				conf->T=atoi(buffer);
				break;
			case 4:
				conf->P=atoi(buffer);
				break;
			case 5:
				conf->S=atoi(buffer);
				break;
			case 6:
				conf->S1=atoi(buffer);
				break;
			case 7:
				conf->S2=atoi(buffer);
				break;
			case 8:
				conf->casseInit=atoi(buffer);
				break;
			case 9:
				conf->intervallo=atoi(buffer);
				break;
			default:
				break;
		}
		++line;
		buffer= tmp;
	}
	if (conf->K <= 0 || conf->casseInit>conf->K || conf->C <= 0 || conf->S1 <= 0 || conf->S2 <= 0){
		fprintf(stderr, "Parametri di configuraizone non validi! Ricontrollare.\n");
		fclose(fd);
		free(buffer);
		exit(EXIT_FAILURE);
	}
	fclose(fd);
	free(buffer);
	printconf(*conf);
	return conf;
}

// STRUCT CODA FIFO
typedef struct node {
	void* info;
	struct node *next;
} node;

typedef struct coda {
	unsigned int size;
	struct node *first;
	struct node *last;
	pthread_mutex_t lock;
	pthread_cond_t cond;
} coda;

// STRUCT DELLA CASSA
typedef struct datiCassa {
	int id;
	int aperta;
	coda *codaClienti;
	pthread_mutex_t lockServito;
	pthread_cond_t condServito;
	pthread_mutex_t lockAperta;
	pthread_cond_t condAperta;
} datiCassa;

// STRUCT DEL CLIENTE
typedef struct datiCliente {
	int id;
	int carrello;
	int *servito;
} datiCliente;

//FUNZIONI CODA
coda* initCoda () {
	coda *q=malloc(sizeof(coda));
	if (!q) return NULL;
	q->first=malloc(sizeof(node));
	if (!q->first) return NULL;
	q->first->info = NULL;
    q->first->next = NULL;
    q->last = q->first;
	q->size=0;
	if (pthread_mutex_init(&q->lock, NULL) != 0) {
		perror("Error inizializing mutex in queue");
		return NULL;
    }
    if (pthread_cond_init(&q->cond, NULL) != 0) {
		perror("Error initializing varcond in queue");
		if (&q->lock) pthread_mutex_destroy(&q->lock);
		return NULL;
    }
	return q;
}

// AGGIUNGE IN CODA
int push (coda*q, void *data) {
	node *ins=malloc(sizeof(node));
	if (!q || !data || !ins) return -1;
	ins->info=data;
	ins->next=NULL;

	pthread_mutex_lock(&q->lock);
	//q->last->next=ins;
	//q->last=ins;
	if (q->size==0) {
		q->first->next=ins;
		q->last=ins;
	} else {
		q->last->next=ins;
		q->last=ins;
	}
	(q->size)++;
	pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->lock);
	return 0;
}

// ESTRAE DALLA TESTA
void* pop (coda*q) {
	if (q==NULL) return NULL;
	pthread_mutex_lock(&q->lock);
	while (q->size == 0) {
		pthread_cond_wait(&q->cond, &q->lock);
	}
	assert(q->first->next);
	node*n=(node*)q->first;
	void*data=(q->first->next)->info;
	q->first=q->first->next;
	(q->size)--;
	assert(q->size>=0);
	pthread_mutex_unlock(&q->lock);
	free((void*)n);
	return data;
}

// CALCOLA LA LUNGHEZZA DELLA CODA
int getSize (coda *q) {
	if (q==NULL) return -1;
	int size=-1;
	pthread_mutex_lock(&q->lock);
	size=q->size;
	pthread_mutex_unlock(&q->lock);
	return size;
}

// DEALLOCA LA CODA
void deleteCoda (coda*q) {
	while (q->first != q->last) {
		node *killer=(node*)q->first;
		q->first=q->first->next;
		free((void*)killer);
	}
	if (q->first) free((void*)((void*)q->first));
	if (&q->lock) pthread_mutex_destroy(&q->lock);
    if (&q->cond) pthread_cond_destroy(&q->cond);
    free(q);
}
