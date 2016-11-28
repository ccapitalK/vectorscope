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

void swapDC(double complex * a, double complex * b){
    double complex temp = *a;
    *a=*b;
    *b=temp;
}

double sampleToOrd(double s, double max){
    return ((s/(double)INT16_MAX)+1.0)*max/2;
}

void drawVisualizer(int width, int height, int readBuffer, double * leftArr, double * rightArr){
    int pos = 0;
    //const ALLEGRO_COLOR wavCol=al_map_rgb(255, 255, 255);
    //const ALLEGRO_COLOR bkgndCol=al_map_rgb(255, 25, 25);
    //const ALLEGRO_COLOR fftCol=al_map_rgb(216, 19, 19);
    const ALLEGRO_COLOR wavCol=al_map_rgb(255, 255, 255);
    const ALLEGRO_COLOR bkgndCol=al_map_rgb(29, 116, 239);
    al_draw_filled_rectangle(0, 0, width, height, bkgndCol);
    
    //draw the oscilloscope
    double px = sampleToOrd(  leftArr[0],width);
    double py = sampleToOrd(-rightArr[0],height);
    for(int i = 1; i < FRAGLENGTH; ++i){
        const double x = sampleToOrd(  leftArr[i],width);
        const double dx = x-px;
        const double y = sampleToOrd(-rightArr[i],height);
        const double dy = y-py;
        double dist = sqrt(dx*dx+dy*dy);
        double scaleFact = CLAMP(0.0, width/(64.0*dist), 1.0);
        const ALLEGRO_COLOR lineCol=al_map_rgb(255*scaleFact, 93+162*scaleFact, 239+16*scaleFact);
        if(x==px && y == py){
        }else{
            al_draw_line(px,py,x,y,lineCol,1);
        }
        px=x;
        py=y;
    }
    for(int i = 0; i < FRAGLENGTH; ++i){
        const double x = sampleToOrd(  leftArr[i],width);
        const double y = sampleToOrd(-rightArr[i],height);
        al_draw_pixel(x,y,wavCol);
    }
    //for(int ix=0; ix<width; ++ix){
    //    int16_t minLNormalized=((int)(minL[ix])*height)/(2*SHRT_MAX);
    //    int16_t minRNormalized=((int)(minR[ix])*height)/(2*SHRT_MAX);
    //    int16_t maxLNormalized=((int)(maxL[ix])*height)/(2*SHRT_MAX);
    //    int16_t maxRNormalized=((int)(maxR[ix])*height)/(2*SHRT_MAX);
    //    if((height/2-minLNormalized)!=(height/2-maxLNormalized)){
    //        al_draw_line(1+ix,height/2-minLNormalized,1+ix,height/2-maxLNormalized,wavCol,1);
    //    }else{
    //        al_draw_pixel(1+ix,height/2-minLNormalized,wavCol);
    //    }
    //    if((height/2-minRNormalized)!=(height/2-maxRNormalized)){
    //        al_draw_line(1+ix,height/2-minRNormalized,1+ix,height/2-maxRNormalized,wavCol,1);
    //    }else{
    //        al_draw_pixel(1+ix,height/2-minRNormalized,wavCol);
    //    }
    //}
}

void * renderLoop(void * parameter){
    double *  leftArr = malloc(sizeof(double)*FRAGLENGTH);
    double * rightArr = malloc(sizeof(double)*FRAGLENGTH);
    int width=640;
    int height=640;
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

//int16_t buf[FRAGLENGTH][2][2] = { 0 };// [n][channel][buffer]
    while(!quit){
        sem_wait(&lock[readBuffer]);
        for(unsigned int i = 0; i < FRAGLENGTH; ++i){
            leftArr [i]=buf[i][0][readBuffer];
            rightArr[i]=buf[i][1][readBuffer];
        }
        drawVisualizer(width, height, readBuffer, leftArr, rightArr);
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
    free( leftArr);
    free(rightArr);
    al_destroy_event_queue(ev_queue);
    al_destroy_display(display);
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
    //assert(reverseByteOrder(2,4)==4);
    //assert(reverseByteOrder(4,5)==4);
    //assert(reverseByteOrder(255,8)==255);
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
