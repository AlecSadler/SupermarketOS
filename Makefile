CC		=  gcc
CFLAGS  = -lpthread -Wall -std=c99
PARAM = test/config.txt

.PHONY: test clean

compile: supermercato.c
	$(CC) supermercato.c -o supermercato.o $(CFLAGS)

test:
	@echo "...Esecuzione 25 secondi..."
	@(./supermercato.o $(PARAM) & echo $$! >supermercato.PID) & sleep 25
	kill -1 $$(cat supermercato.PID)
	while ps -p $$(cat supermercato.PID) > /dev/null; do sleep 1; done
	chmod +x ./analisi.sh
	./analisi.sh

clean:
	rm -f *.o *.log *.PID
