//Jan Freymann
// November 2013

// http://cpsnd.wordpress.com


#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include "math.h"
//OSC stuff:

#include "lo/lo.h"

//timing:

#include "time.h"
double lux(int ch0, int ch1);

//methoden:

void openPort(int *fd); //greife auf i2c treiber zu
void addSlave(int address, int *fd); //füge i2c slave hinzu
double readLight(int* fd, int *cLow, int *cHigh); //lese Lichtwert
void sendOSC(lo_address *t, char *msg, double param);
lo_address initOSC(char *addr, char *port);

void openPort(int *fd) {
    if((*fd = open("/dev/i2c-1", O_RDWR)) < 0) {
        printf("Kann i2c port 1 nicht oeffnen.\n");
    }
}

void addSlave(int address, int *fd) {
    if(ioctl(*fd, I2C_SLAVE, address) < 0) {
        printf("Kann nicht auf Bus zugreifen.\n");
    }
}

double readLight(int* fd, int *cLow, int *cHigh) {
    unsigned char buf[10];
    buf[0] = 0xAC;

    if((write(*fd, buf, 1)) != 1) {
        printf("Kann Register nicht an Chip senden.\n");
    }

    if(read(*fd, buf, 4) != 4) {
        printf("Kann nicht von Chip lesen.\n");
    }
    unsigned char highByte = buf[2];
    unsigned char lowByte = buf[3];
    unsigned int ch0 = 256 * highByte + lowByte;

    //automatische kalibrierung:
    if(ch0 > *cHigh) {
        *cHigh = ch0;
    }
    if(ch0 < *cLow) {
        *cLow = ch0;
    }

    double cc = (ch0 - *cLow) / ((double) *cHigh);
    return cc;
}

int main(int argc, char** argv) {
    printf("i2c c test.\n");
    int fd[2];
    unsigned char buf[10];
    char *fileName = "/dev/i2c-1";
    int addr[2] = {0x29, 0x39}; // Adressen der Chips

    openPort(&fd[0]);
    openPort(&fd[1]);
    addSlave(addr[0], &fd[0]);
    addSlave(addr[1], &fd[1]);

    int i;

    int readcount[2] = {0, 1};
    double maxlux = 0.0;
    int maxCh0 = 0;


    //Variablen zur Berechnung des Lichteinfalls
    int cLow[2] = {65536, 65536};
    int cHigh[2] = {0, 0};
    double cc[2] = {0.0, 0.0};
    double maxcc[2] = {0.0, 0.0};
    double lastcc[2] = {0.0, 0.0};
    double epsilon = 0.01;

    //Variablen zur Aktivierung der Dosen:

    int active[2] = {0, 0};
    double actThr = 0.1; //Schwellenwert um Verdunklung zu erkennen

    //initialisiere OSC:

    lo_address oscaddr = initOSC("192.168.1.8", "7777");

    while(1) {
        for(i = 0; i < 2; i++) {
            cc[i] = readLight(&fd[i], &cLow[i], &cHigh[i]);
            readcount[i]++;
            if(cc[i] > maxcc[i]) {
                maxcc[i] = cc[i];
            }
            if(readcount[i] >= 3) {
                if(active[i]) {
                    if((lastcc[i] > maxcc[i] + epsilon) || (lastcc[i] < maxcc[i] - epsilon)) {
                        //Lichtintensitaet hat sich signifikant geaendert -> Ausgabe!
                        printf("%d Licht: %f\n", i, maxcc[i]);
                        if(i == 0) sendOSC(&oscaddr, "/licht0", maxcc[i]);
                        else if(i == 1) sendOSC(&oscaddr, "/licht1", maxcc[i]);
                        lastcc[i] = maxcc[i];
                    }
                }
                else {
                    if(maxcc[i] > (1.0 - actThr)) {
                        printf("Dose %d ist jetzt aktiv.\n", i);
                        active[i] = 1;
                    }
                }
                readcount[i] = 0;
                maxcc[i] = 0.0;
            }
        }

        /* lo_address t = lo_address_new("192.168.1.8", "7777");

        if(lo_send(t, "/light", "f", cc) == -1) {
        printf("OSC Fehler: %s\n", lo_address_errstr(t));
        }*/
        
        usleep(13 * 1000);

    }
    return 0;
}
lo_address initOSC(char *addr, char *port) {
    return lo_address_new(addr, port);
}

void sendOSC(lo_address *t, char *msg, double param) {
    if(lo_send(*t, msg, "f", param) == -1) {
        printf("OSC Fehler: %s\n", lo_address_errstr(*t));
    }
}
