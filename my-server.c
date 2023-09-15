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

#define BUF_SIZE 1024
#define PORT_NO 5000
#define MAX_CLIENTS 10

Display *display;
Window window;
XButtonEvent buttonEvent;
XEvent event;
GC gc;
XFontStruct *font_info;
int screen;
bool hasNext;
struct Stroke *strokeRoot = NULL;
fd_set readSet;
struct timeval timeout;
XPoint startPoint;

void onEvent();
struct Stroke *appendStroke(int x0, int y0, int x1, int y1);

bool parse(const char *input)
{
    XPoint p0, p1;
    char dataType;
    char data[BUF_SIZE];
    int parsed = sscanf(input, "%c-%s", &dataType, data);

    if (parsed != 2)
    {
        perror("parse");
        exit(EXIT_FAILURE);
    }

    switch (dataType)
    {
    case 'S':
        if (strcmp(data, "quit") == 0)
            return true;
        break;

    case 'M':
        printf("%s\n", data);
        break;

    case 'D':
        parsed = sscanf(data, "%hd-%hd-%hd-%hd", &p0.x, &p0.y, &p1.x, &p1.y);
        XDrawLine(display, window, gc, p0.x, p0.y, p1.x, p1.y);
        appendStroke(p0.x, p0.y, p1.x, p1.y);
        break;
    }

    return false;
}

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
        appendStroke(startPoint.x, startPoint.y,
                     event.xmotion.x, event.xmotion.y);
        startPoint.x = event.xmotion.x;
        startPoint.y = event.xmotion.y;
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
    XSetForeground(display, gc, 0xffa500);
}

int serverSocket, clientSocket;
struct sockaddr_in serverAddr, clientAddr;
socklen_t clientLen = sizeof(clientAddr);
char buffer[BUF_SIZE];

int main()
{
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT_NO);
    if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    listen(serverSocket, MAX_CLIENTS);
    printf("Server is listening on port %d...\n", PORT_NO);

    createWindow(400, 300, "Server");

    int clients[MAX_CLIENTS] = {0};

    hasNext = true;
    timeout.tv_sec = 0;
    timeout.tv_usec = 5;

    printf("Command List:\n");
    printf(":q - shut down\n");

    while (1)
    {
        onEvent();

        FD_ZERO(&readSet);
        FD_SET(serverSocket, &readSet);
        FD_SET(STDIN_FILENO, &readSet);

        int maxfd = serverSocket;

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i] != 0)
            {
                FD_SET(clients[i], &readSet);
                if (clients[i] > maxfd)
                    maxfd = clients[i];
            }
        }

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
                buffer[bytesRead] = '\0';

            if (strcmp(buffer, ":q\n") == 0)
            {
                printf("The server has been shut down.\n");
                hasNext = false;
            }
        }

        if (FD_ISSET(serverSocket, &readSet))
        {
            clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientLen);
            if (clientSocket < 0)
            {
                perror("accept");
                exit(EXIT_FAILURE);
            }

            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (clients[i] == 0)
                {
                    clients[i] = clientSocket;
                    printf("Client[%d] connected from %s:%d\n", i, inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));

                    sprintf(buffer, "%d", i);
                    write(clients[i], buffer, sizeof(buffer));
                    break;
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i] != 0 && FD_ISSET(clients[i], &readSet))
            {
                ssize_t bytesRead = read(clients[i], buffer, sizeof(buffer));
                if (bytesRead > 0)
                {
                    bool isDisconect = parse(buffer);
                    if (!isDisconect)
                    {
                        for (int j = 0; j < MAX_CLIENTS; j++)
                        {
                            if (clients[j] != 0 && i != j)
                                write(clients[j], buffer, sizeof(buffer));
                        }
                    }
                    else
                    {
                        close(clients[i]);
                        clients[i] = 0;
                        printf("client[%d] has exited from the server.\n", i);
                    }
                }
            }
        }

        if (!hasNext)
            break;
    }

    close(serverSocket);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] != 0)
            close(clients[i]);
    }

    XFreeGC(display, gc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    return 0;
}
