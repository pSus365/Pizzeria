# Makefile

CC = gcc
CFLAGS = -Wall -pthread

MAIN_EXE = main
KASJER_EXE = kasjer
KLIENT_EXE = klient
STRAZAK_EXE = strazak

MAIN_SRC = main.c
KASJER_SRC = kasjer.c
KLIENT_SRC = klient.c
STRAZAK_SRC = strazak.c

all: $(MAIN_EXE) $(KASJER_EXE) $(KLIENT_EXE) $(STRAZAK_EXE)

$(MAIN_EXE): $(MAIN_SRC)
	$(CC) $(CFLAGS) -o $(MAIN_EXE) $(MAIN_SRC)

$(KASJER_EXE): $(KASJER_SRC)
	$(CC) $(CFLAGS) -o $(KASJER_EXE) $(KASJER_SRC)

$(KLIENT_EXE): $(KLIENT_SRC)
	$(CC) $(CFLAGS) -o $(KLIENT_EXE) $(KLIENT_SRC)

$(STRAZAK_EXE): $(STRAZAK_SRC)
	$(CC) $(CFLAGS) -o $(STRAZAK_EXE) $(STRAZAK_SRC)

clean:
	rm -f $(MAIN_EXE) $(KASJER_EXE) $(KLIENT_EXE) $(STRAZAK_EXE) report.txt