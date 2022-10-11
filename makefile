
CC := gcc
CFLAGS := -pthread

# Per compilare i file server.c e client.c
all: client server


server : server.c
	$(CC) $(CFLAGS) $< -o $@.out


client : client.c
	$(CC) $(CFLAGS) $< -o $@.out


.PHONY: all clean exec


# Per rimuovere tutti i file .txt usati come log locali/globali
clean:
	rm -f *_logLocale.txt
	rm -f logGlobale.txt

exec:
	rm -f *.out
