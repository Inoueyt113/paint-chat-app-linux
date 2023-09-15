#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/times.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>

#define SERVER_IP "127.0.0.1"
#define PORT_NO 5000
#define BUF_SIZE 1024

int clientSocket;
struct sockaddr_in serverAddr;
char buffer[BUF_SIZE];
bool hasNext;
Display *display;
Window window;
XButtonEvent buttonEvent;
XEvent event;
GC gc;
XFontStruct *font_info;
int screen;
struct Stroke *strokeRoot = NULL;
fd_set readSet;
struct timeval timeout;
XPoint startPoint;

struct Stroke
{
    XPoint p0;
    XPoint p1;
    struct Stroke *next;
};

struct Stroke *appendStroke(int x0, int y0, int x1, int y1)
{
    struct Stroke *newStroke = (struct Stroke *)calloc(1, sizeof(struct Stroke));
    newStroke->p0.x = x0;
    newStroke->p0.y = y0;
    newStroke->p1.x = x1;
    newStroke->p1.y = y1;
    newStroke->next = strokeRoot;
    strokeRoot = newStroke;

    return strokeRoot;
}

void onEvent();
int createWindow(int x, int y, const char *title);

void parse(const char *input)
{
    XPoint p0, p1;
    char dataType;
    char data[BUF_SIZE];
    int parsed = sscanf(input, "%c-%s", &dataType, data);

    switch (dataType)
    {
    case 'M':
        printf("%s\n", data);
        break;

    case 'D':
        parsed = sscanf(data, "%hd-%hd-%hd-%hd", &p0.x, &p0.y, &p1.x, &p1.y);
        XDrawLine(display, window, gc, p0.x, p0.y, p1.x, p1.y);
        appendStroke(p0.x, p0.y, p1.x, p1.y);
        break;
    }
}

int createWindow(int x, int y, const char *title)
{
    display = XOpenDisplay(NULL);
    if (display == NULL)
    {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }

    screen = DefaultScreen(display);

    window = XCreateSimpleWindow(display, RootWindow(display, screen), 10, 10, x, y, 1,
                                 BlackPixel(display, screen), WhitePixel(display, screen));

    int lineWidth = 3;
    XMapWindow(display, window);
    XFlush(display);
    XStoreName(display, window, title);
    XSelectInput(display, window, ExposureMask | ButtonPressMask | ButtonMotionMask | KeyPressMask);
    gc = XCreateGC(display, window, 0, NULL);
    XSetLineAttributes(display, gc, lineWidth, LineSolid, CapRound, JoinRound);
    font_info = XLoadQueryFont(display, "fixed");
    XSetFont(display, gc, font_info->fid);
    XSetForeground(display, gc, BlackPixel(display, screen));
    XSetForeground(display, gc, 0x000000);
}

int serverSocket, clientSocket;
struct sockaddr_in serverAddr, clientAddr;
socklen_t clientLen = sizeof(clientAddr);
char buffer[BUF_SIZE];

void onEvent()
{
    if (XEventsQueued(display, QueuedAfterFlush) == 0)
        return;
    XNextEvent(display, &event);
    switch (event.type)
    {
    case Expose:
        struct Stroke *stroke;
        for (stroke = strokeRoot; stroke != NULL; stroke = stroke->next)
            XDrawLine(display, window, gc, stroke->p0.x, stroke->p0.y, stroke->p1.x, stroke->p1.y);
        break;
    case ButtonPress:
        startPoint.x = event.xbutton.x;
        startPoint.y = event.xbutton.y;
        break;
    case MotionNotify:
        XDrawLine(display, window, gc, startPoint.x, startPoint.y,
                  event.xmotion.x, event.xmotion.y);

        // 描画する点を文字列としてサーバーへ送信
        char drawData[BUF_SIZE];
        snprintf(drawData, sizeof(drawData), "D-%d-%d-%d-%d", startPoint.x, startPoint.y,
                 event.xmotion.x, event.xmotion.y);

        write(clientSocket, drawData, sizeof(drawData));

        // appendStroke(startPoint.x, startPoint.y,
        //              event.xmotion.x, event.xmotion.y);
        startPoint.x = event.xmotion.x;
        startPoint.y = event.xmotion.y;
        break;
    }
}

int main()
{
    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT_NO);
    inet_aton(SERVER_IP, &serverAddr.sin_addr);
    if (connect(clientSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        perror("connect");
        exit(EXIT_FAILURE);
    }
    printf("Connection to the server was successful.");
    char clientId[2];

    ssize_t bytesRead = read(clientSocket, buffer, sizeof(buffer));
    if (bytesRead > 0)
    {
        strcpy(clientId, buffer);
        printf("Your clientID is %s.\n", clientId);
    }

    char title[11 + 2];
    snprintf(title, sizeof(title), "Client[%s]", clientId);

    createWindow(400, 300, title);

    fd_set readSet;
    int maxfd = clientSocket;
    timeout.tv_sec = 0;
    timeout.tv_usec = 5;
    hasNext = true;

    printf("Command List:\n");
    printf(":q - Quit\n");

    while (1)
    {
        onEvent();

        FD_ZERO(&readSet);
        FD_SET(clientSocket, &readSet);
        FD_SET(STDIN_FILENO, &readSet);

        int ready = select(maxfd + 1, &readSet, NULL, NULL, &timeout);
        if (ready == -1)
        {
            perror("select");
            break;
        }

        if (FD_ISSET(STDIN_FILENO, &readSet))
        {
            ssize_t bytesRead = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (bytesRead > 0)
            {
                buffer[bytesRead] = '\0';
                if (strcmp(buffer, ":q\n") == 0)
                {
                    puts("The connection has been terminated.\n");
                    write(clientSocket, "S-quit", sizeof("S-quit"));
                    hasNext = false;
                }
                else
                {
                    char tempBuf[BUF_SIZE - 18];
                    strcpy(tempBuf, buffer);
                    snprintf(buffer, sizeof(buffer), "M-Client[%s]:%s", clientId, tempBuf);
                    write(clientSocket, buffer, sizeof(buffer));
                }
            }
        }

        if (FD_ISSET(clientSocket, &readSet))
        {
            ssize_t bytesRead = read(clientSocket, buffer, sizeof(buffer));
            if (bytesRead > 0)
            {
                parse(buffer);
            }
        }

        if (!hasNext)
            break;
    }

    close(clientSocket);

    return 0;
}
