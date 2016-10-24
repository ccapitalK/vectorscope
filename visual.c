#include<allegro5/allegro.h>
#include<allegro5/allegro_primitives.h>
#include<stdio.h>
#include<stdint.h>
#include<signal.h>
#include<errno.h>
#include<semaphore.h>
#include<pthread.h>
#include<limits.h>
#include<unistd.h>
#include<complex.h>
#include<math.h>
#include<assert.h>
#include<string.h>

#define ERROR(x) {fprintf(stderr, "Error in %s(): " x "\n", __func__);exit(EXIT_FAILURE);}
#define PARAMETER_ERROR() ERROR("Invalid Parameters provided");
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define CLAMP(min,val,max) MAX((min),MIN((max),(val)))

//44100/20 = 2205
//#define FRAGLENGTH 2205

//framerate doesn't need to be exact, but 
//FRAGLENGTH needs to be power of two
#define FRAGLENGTH 2048
//channel: 0=left, 1=right
int16_t buf[FRAGLENGTH][2][2] = { 0 };// [n][channel][buffer]
int n=0;
sem_t lock[2];
pthread_t renderThread;

int quit=0;

int fgetw(FILE * fp){
    int a=fgetc(fp);
    if(a==EOF){
        return EOF;
    }
    int b=fgetc(fp);
    if(b==EOF){
        return EOF;
    }
    uint16_t retval=((b<<8)+a);
    return retval;
}

void fputw(int16_t val, FILE * fp){
    fputc(val&0xffu,fp);
    fputc(val>>8,fp);
}

int roundToNextPow2(int in){
    in--;
    in|=in>>1;
    in|=in>>2;
    in|=in>>4;
    in|=in>>8;
    in|=in>>16;
    return in+1;
}

void compute_dft(double complex in[], double complex out[], int n){
    for(int i = 0; i < n; ++i){
        double sumRe=0.0;
        for(int j = 0; j < n; ++j){
            double angle = 2 * M_PI * i * j / n;
            sumRe+=cos(angle)*in[j];
        }
        out[i]+=sumRe+0.0I;
    }
}

void swapDC(double complex * a, double complex * b){
    double complex temp = *a;
    *a=*b;
    *b=temp;
}

unsigned int reverseByteOrder(unsigned int input, unsigned int numBits){
    unsigned int retVal=0;
    if(input >= (1U << numBits)){
        PARAMETER_ERROR();
    }
    for(unsigned int i = 0; i < numBits; ++i){
        retVal<<=1;
        retVal |= input&1;
        input>>=1;
    }
    return retVal;
}

void compute_fft(double complex data[], unsigned int n){
    //check n is power of two
    int found=0;
    unsigned int logN=1;
    for(unsigned int i = 2; i!=0; i<<=1, logN++){
        if(i==n){
            found=1;
            break;
        }
    }
    if(!found){
        fprintf(stderr, "Error: call to %s(..., n=%d)\n", __func__, n);
        exit(EXIT_FAILURE);
    }
    //sort by reverse byte order
    //In this case, we are doing this in place
    for(unsigned int i = 0; i < n; ++i){
        unsigned int rev = reverseByteOrder(i,logN);
        if(i<rev){
            swapDC(&data[i],&data[rev]);
        }
    }
    //Perform Danielson Lanczos fft
    unsigned int mmax = 1;
    while(n > mmax){
        unsigned int istep = 2 * mmax;
        double theta = -M_PI/mmax;
        double temp = sin(theta/2.0);
        double complex wp = -2.0*temp*temp + sin(theta)*I;
        double complex w = 1.0 + 0.0I;
        for(unsigned int m=0; m<mmax; ++m){
            for(unsigned int i=m; i<n; i+=istep){
                unsigned int j = i+mmax;
                double complex temp = w * data[j];
                data[j]=data[i]-temp;
                data[i]=data[i]+temp;
            }
            w = w * wp + w;
        }
        mmax = istep;
    }
}

void drawVisualizer(uint16_t width, uint16_t height, int readBuffer, double complex * leftArr[2], double complex * rightArr[2], int arrLength, unsigned int flip){
    int pos = 0;
    int16_t max[width];
    int16_t min[width];
    //const ALLEGRO_COLOR wavCol=al_map_rgb(255, 255, 255);
    //const ALLEGRO_COLOR bkgndCol=al_map_rgb(255, 25, 25);
    //const ALLEGRO_COLOR fftCol=al_map_rgb(216, 19, 19);
    const ALLEGRO_COLOR wavCol=al_map_rgb(255, 255, 255);
    const ALLEGRO_COLOR bkgndCol=al_map_rgb(29, 116, 239);
    const ALLEGRO_COLOR fftCol=al_map_rgb(0, 93, 224);
    al_draw_filled_rectangle(0, 0, width, height, bkgndCol);
    for(uint16_t ix=0; ix<width; ++ix){
        max[ix]=SHRT_MIN;
        min[ix]=SHRT_MAX;
        if(width<=FRAGLENGTH){
            while(pos<((ix+1)*FRAGLENGTH)/(width)){
                max[ix]=MAX(max[ix],buf[pos][0][readBuffer]);
                max[ix]=MAX(max[ix],buf[pos][1][readBuffer]);
                min[ix]=MIN(min[ix],buf[pos][0][readBuffer]);
                min[ix]=MIN(min[ix],buf[pos][1][readBuffer]);
                leftArr [flip&1][pos]=buf[pos][0][readBuffer];
                rightArr[flip&1][pos]=buf[pos][1][readBuffer];
                ++pos;
            }
        }else{
            pos = (ix * FRAGLENGTH)/width;
            max[ix]=MAX(max[ix],buf[pos][0][readBuffer]);
            max[ix]=MAX(max[ix],buf[pos][1][readBuffer]);
            min[ix]=MIN(min[ix],buf[pos][0][readBuffer]);
            min[ix]=MIN(min[ix],buf[pos][1][readBuffer]);
            leftArr [flip&1][pos]=buf[pos][0][readBuffer];
            rightArr[flip&1][pos]=buf[pos][1][readBuffer];
        }
        //max[ix]=MIN(max[ix],0);
        //min[ix]=MAX(min[ix],0);
    }
    compute_fft( leftArr[flip&1],arrLength);
    compute_fft(rightArr[flip&1],arrLength);
    int transformRects=128;
    pos=0;
    for(uint16_t i=0; i < transformRects; ++i){

        uint16_t x0=(i*(width-1))/transformRects;
        uint16_t x1=((i+1)*(width-1))/transformRects;

        double rectAmpL=0.0f;
        double rectAmpR=0.0f;
        int numInChunk=0;

        while(pos<(transformRects*(i+1))/(transformRects)){
            rectAmpL +=  leftArr[(flip&1)^0][pos]*0.8;
            rectAmpR += rightArr[(flip&1)^0][pos]*0.8;
            rectAmpL +=  leftArr[(flip&1)^1][pos]*0.2;
            rectAmpR += rightArr[(flip&1)^1][pos]*0.2;
            ++pos;
            ++numInChunk;
        }
        rectAmpL/=(double)numInChunk;
        rectAmpR/=(double)numInChunk;

        uint16_t thisHeightl=CLAMP(0,sqrt(fabs(rectAmpL))/(sqrt(arrLength)/2),160);
        uint16_t thisHeightr=CLAMP(0,sqrt(fabs(rectAmpR))/(sqrt(arrLength)/2),160);
        al_draw_filled_rectangle(x0,1,x1,1+thisHeightl,               fftCol);
        al_draw_filled_rectangle(x0,height-1-thisHeightr,x1,height-1, fftCol);
        //al_draw_filled_rectangle(x0,height/2-thisHeightl,x1,height/2, fftCol);
        //al_draw_filled_rectangle(x0,height/2,x1,height/2+thisHeightr, fftCol);
    }
    for(uint16_t ix=0; ix<width; ++ix){
        int16_t minNormalized=((int)(min[ix])*height)/(2*SHRT_MAX);
        int16_t maxNormalized=((int)(max[ix])*height)/(2*SHRT_MAX);
        al_draw_line(1+ix,height/2+minNormalized,1+ix,height/2+maxNormalized,wavCol,1);
    }
}

void * renderLoop(void * parameter){
    unsigned int fftArrLength=roundToNextPow2(FRAGLENGTH);
    double complex * leftArr [2];
    double complex * rightArr[2];
    uint16_t width=640;
    uint16_t height=320;
    leftArr [0] = malloc(sizeof(double complex)*fftArrLength);
    leftArr [1] = malloc(sizeof(double complex)*fftArrLength);
    rightArr[0] = malloc(sizeof(double complex)*fftArrLength);
    rightArr[1] = malloc(sizeof(double complex)*fftArrLength);
    ALLEGRO_DISPLAY * display;
    if(al_init()==0){
        ERROR("Couldn't initialize Allegro");
        exit(EXIT_FAILURE);
    }
    ALLEGRO_EVENT_QUEUE * ev_queue = al_create_event_queue();

    al_set_new_display_flags(ALLEGRO_RESIZABLE | ALLEGRO_NOFRAME);
    display = al_create_display(width, height);
    al_register_event_source(ev_queue, al_get_display_event_source(display));
    int readBuffer=1;

    unsigned int flip=0;
    for(unsigned int i = 0; i < fftArrLength; ++i){
        leftArr [1][i]=0.0+0.0I;
        rightArr[1][i]=0.0+0.0I;
    }

    while(!quit){
        for(unsigned int i = 0; i < fftArrLength; ++i){
            leftArr [flip&1][i]=0.0+0.0I;
            rightArr[flip&1][i]=0.0+0.0I;
        }
        ++flip;
        sem_wait(&lock[readBuffer]);
        drawVisualizer(width, height, readBuffer, leftArr, rightArr, fftArrLength, flip);
        al_flip_display();
        sem_post(&lock[readBuffer]);
        {
            ALLEGRO_EVENT ev;
            while(al_get_next_event(ev_queue, &ev)){
                switch(ev.type){
                    case ALLEGRO_EVENT_DISPLAY_RESIZE:
                        width=ev.display.width;
                        height=ev.display.height;
                        al_acknowledge_resize(display);
                        break;
                    case ALLEGRO_EVENT_DISPLAY_CLOSE:
                        quit=1;
                        break;
                }
            }
        }
        readBuffer=!readBuffer;
    }
    al_destroy_event_queue(ev_queue);
    al_destroy_display(display);
    free( leftArr[0]);
    free( leftArr[1]);
    free(rightArr[0]);
    free(rightArr[1]);
    return NULL;
}

void getMonitorName(char * dest){
    FILE * pactl = popen("pactl list short sinks", "r");
    if(pactl==NULL){
        ERROR("Could not open pactl");
        exit(EXIT_FAILURE);
    }
    char dataBuf[2048];
    char * monitors[32];
    monitors[0]=dataBuf;
    int nMonitors=1;
    int n = 0;
    char b;
    while((b=fgetc(pactl))!=EOF){
        dataBuf[n++]=b;
    }
    dataBuf[n]='\0';
    for(int i = 0; i < n; ++i){
        if(dataBuf[i]=='\n'&&i+1<n){
            monitors[nMonitors++]=&dataBuf[i+1];
            dataBuf[i]='\0';
        }
    }
    for(int i = 0; i < nMonitors; ++i){
        if(strstr(monitors[i],"RUNNING")!=NULL){
            strtok(monitors[i]," \t");
            char * name = strtok(NULL," \t");
            strncpy(dest,name,256);
        }
    }
    pclose(pactl);
}

void tests(){
    assert(reverseByteOrder(2,4)==4);
    assert(reverseByteOrder(4,5)==4);
    assert(reverseByteOrder(255,8)==255);
}

pid_t mpopen(const char * command, int * in, int * out){
    const int READ=0, WRITE=1;
    int nStdin[2], nStdout[2];
    pid_t pid;
    if(pipe(nStdin) != 0 || pipe(nStdout) != 0){
        return -1;
    }

    pid = fork();
    if(pid<0){
        return pid;
    }else if(pid == 0){
        close(nStdin[WRITE]);
        dup2(nStdin[READ], fileno(stdin));
        close(nStdout[READ]);
        dup2(nStdout[WRITE], fileno(stdout));
        execl("/bin/sh", "sh", "-c", command, NULL);
        ERROR("execl() failed to execute, maybe 'pacat' isn't installed?");
        exit(EXIT_FAILURE);
    }

    if(in == NULL){
        close(nStdin[WRITE]);
    }else{
        *in=nStdin[WRITE];
    }

    if(out == NULL){
        close(nStdout[READ]);
    }else{
        *out=nStdout[READ];
    }

    return pid;
}

int main(int argc, char * argv[]){
    tests();
    char cmdbuf[256] = { 0 };
    char inputMonitorName[256] = { 0 };
    if(argc<2){
        getMonitorName(inputMonitorName);
        if(!inputMonitorName[0]){
            ERROR("Could not find running pulse sink");
            return EXIT_FAILURE;
        }
        sprintf(cmdbuf, "pacat --raw --record --latency-msec=10 --format s16le -d %s.monitor", inputMonitorName);
    }else{
        sprintf(cmdbuf, "pacat --raw --record --latency-msec=10 --format s16le -d %s", argv[1]);
    }

    //FILE * inputDevice = popen(cmdbuf, "r");
    int fd;
    pid_t childPID = mpopen(cmdbuf, NULL, &fd);
    FILE * inputDevice = fdopen(fd, "r");
    if(inputDevice==NULL){
        ERROR("Couldn't popen(\"pacat ...\")");
        fprintf(stderr, "ERRNO: %d\n", errno);
        return EXIT_FAILURE;
    }
    if(sem_init(&lock[0],0,1)==-1||sem_init(&lock[1],0,1)){
        ERROR("Couldn't initialize semaphores");
        fprintf(stderr, "ERRNO: %d\n", errno);
        return EXIT_FAILURE;
    }

    if(pthread_create(&renderThread, NULL, renderLoop, NULL)!=0){
        ERROR("Spawn Render() thread");
        fprintf(stderr, "ERRNO: %d\n", errno);
        return EXIT_FAILURE;
    }
    int channel=0;
    int writeBuffer=0;
    sem_wait(&lock[writeBuffer]);
    for(int b=fgetw(inputDevice); !quit&&b!=EOF; b=fgetw(inputDevice)){
        buf[channel?n++:n][channel][writeBuffer]=b;
        channel=!channel;
        if(n==FRAGLENGTH){
            sem_post(&lock[writeBuffer]);
            writeBuffer=!writeBuffer;
            sem_wait(&lock[writeBuffer]);
            n=0;
        }
    }
    sem_post(&lock[writeBuffer]);
    quit=1;
    kill(childPID, SIGINT);
    pthread_join(renderThread, NULL);
    return EXIT_SUCCESS;
}
