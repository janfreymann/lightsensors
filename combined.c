//Jan Freymann
// January 2014

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

#include "time.h"
#include "lo/lo.h"

struct vect { double x; double y; double z; };

typedef struct vect dvector;

//timing:

double lux(int ch0, int ch1);

int magnetMeasures = 0;
dvector magnetBase;

//methoden:

void openPort(int *fd); //greife auf i2c treiber zu
void addSlave(int address, int *fd); //füge i2c slave hinzu
double readLight(int* fd, int *cLow, int *cHigh); //lese Lichtwert
void sendOSC(lo_address *t, char *msg, double param);
lo_address initOSC(char *addr, char *port);

void sendMagnetRead(int *fd);
dvector readMagnetVector(int *fd);
double convert2sComp(unsigned char a, unsigned char b);
double formatAngle(double rad);

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

void sendMagnetRead(int *fd) {  
    unsigned char buf[10];
    buf[0] = 0x02;
    buf[1] = 0x01;

    if((write(*fd, buf, 2)) != 2) {
        printf("Kann Register nicht an Chip senden.\n");
    }
}
dvector readMagnetVector(int *fd) { 
    dvector vc; 
    unsigned char buf[10];
    buf[0] = 0x06; //todo ??

    if((write(*fd, buf, 1)) != 1) {
        printf("Kann Register nicht an Chip senden.\n");
    }
    
    if(read(*fd, buf, 6) != 6) {
        printf("Kann nicht von Chip lesen.\n");
    }
    vc.x = convert2sComp(buf[0], buf[1]);
    vc.z = convert2sComp(buf[2], buf[3]);
    vc.y = convert2sComp(buf[4], buf[5]);

    //normalize vector:

    double length = sqrt(vc.x*vc.x + vc.y*vc.y + vc.z * vc.z);
    vc.x /= length;
    vc.y /= length;
    vc.z /= length;
/*
    //lose precision (remove noise)

    vc.x = (double) ((int) ( vc.x * 10));
    vc.y = (double) ((int) (vc.y * 10));
    vc.z = (double) ((int) ( vc.z * 10));

    printf("Magnet: %f %f %f %f\n", vc.x, vc.y, vc.z, length);
*/
    //compute angles:

    double phi1 = formatAngle(atan2(vc.y, vc.x));
    double phi2 = formatAngle(atan2(vc.z, vc.x));
    printf("Manget angles: %f %f\n", phi1, phi2);

    return vc;
}
double formatAngle(double rad) {
    //if(rad < 0.0) { return formatAngle(M_PI + rad); }
    //else if(rad >2* M_PI) { return formatAngle(rad - M_PI); }
    return rad;
}
double convert2sComp(unsigned char msb, unsigned char lsb) {
    long t = msb * 0x100L + lsb;	
    if(t >= 32768) {
        t -= 65536;
    }
    return (double) t;
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
    int fd[3];
    unsigned char buf[10];
    char *fileName = "/dev/i2c-1";
    int addr[3] = {0x29, 0x39, 0x1e}; // Adressen der Chips

    openPort(&fd[0]); //Dose 1
    openPort(&fd[1]); //Dose 2
    openPort(&fd[2]); //Magnetometer

    addSlave(addr[0], &fd[0]);
    addSlave(addr[1], &fd[1]);
    addSlave(addr[2], &fd[2]);

    int i;

    int readcount[2] = {0, 1};
    double maxlux = 0.0;
    int maxCh0 = 0;

    dvector magnetVec;


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

    lo_address oscaddr = initOSC("192.168.1.125", "7777");

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

        sendMagnetRead(&fd[2]);
 
        usleep(13 * 1000);

        magnetVec = readMagnetVector(&fd[2]);
        sendOSC(&oscaddr, "/magnetX", magnetVec.x);
        sendOSC(&oscaddr, "/magnetY", magnetVec.y);
        sendOSC(&oscaddr, "/magnetZ", magnetVec.z);

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
