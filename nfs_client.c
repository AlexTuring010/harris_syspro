// server
#include "basics.h"
void send_list(int fd, const char *dirpath) //στελνει την λιστα των αρχειων του directory στον client
{
    DIR *dir = opendir(dirpath);
    if (!dir)
    {
        dprintf(fd, ".\n");
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)))
    {
        char full_path[MAX_LINE];
        snprintf(full_path, sizeof(full_path), "%s/%s", dirpath, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISREG(st.st_mode)) //ελεγχος αν ειναι αρχειο
        {
            dprintf(fd, "%s\n", entry->d_name);
        }
    }
    dprintf(fd, ".\n");
    closedir(dir);
}

void send_file(int fd, const char *filepath) //στελνει το περιεχομενο του αρχειου στον client
{
    int f = open(filepath, O_RDONLY);
    if (f < 0)
    {
        dprintf(fd, "-1 %s\n", strerror(errno));
        return;
    }
    struct stat st;
    fstat(f, &st);
    dprintf(fd, "%ld ", st.st_size);

    char buf[BUFSIZE];
    ssize_t n;
    while ((n = read(f, buf, BUFSIZE)) > 0)
        write(fd, buf, n);
    close(f);
}

int main(int argc, char *argv[])
{
    if (argc != 5 || strcmp(argv[1], "-p") != 0 || strcmp(argv[3], "-d") != 0) {
        fprintf(stderr, "Usage: %s -p <port> -d <directory>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[2]);
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    // ορισμος ip που θα ακουει το socket
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; //δεχεται οποιαδηποτε ip
    addr.sin_port = htons(port);
    //κατασταση ακροασης εως και 10 ταυτοχρονες συνδεσεις
    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 10);

    int file_out = -1;

    while (1) //περιμενουμε συνδεση,αν οχι αγνοουμε και συνεχιζουμε
    {
        int client = accept(server_fd, NULL, NULL); 
        if (client < 0)
            continue;
        // Διαβάζουμε την εντολη απο τον client(list pull push)
        char input[MAX_LINE] = {0};
        if (read(client, input, sizeof(input)) <= 0)
        {
            close(client);
            continue;
        }
        //cmd= LIST, PULL, PUSH και arg=directory ή αρχείο
        char cmd[16], arg[MAX_LINE];
        sscanf(input, "%s %s", cmd, arg);

        if (strcmp(cmd, "LIST") == 0) 
        {
            send_list(client, arg);
        }
        else if (strcmp(cmd, "PULL") == 0)
        {
            send_file(client, arg);
        }
        else if (strcmp(cmd, "PUSH") == 0)
        {
            char *ptr = strstr(input, arg) + strlen(arg);
            ssize_t size;
            sscanf(ptr, "%ld", &size);

            if (size == -1) 
            {
                file_out = open(arg, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            }
            else if (size == 0)
            {
                if (file_out != -1)
                    close(file_out);
                file_out = -1;
            }
            else if (file_out != -1)
            {
                char buf[BUFSIZE];
                ssize_t received = 0;
                while (received < size)
                {
                    ssize_t r = read(client, buf, size - received);
                    if (r <= 0)
                        break;
                    write(file_out, buf, r);
                    received += r;
                }
            }
        }

        close(client);
    }

    close(server_fd);
    return 0;
}
