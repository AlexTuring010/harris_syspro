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
    while ((n = read(f, buf, BUFSIZE)) > 0) {
        write(fd, buf, n);
    }
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
    
    // Enable SO_REUSEADDR to allow port reuse immediately after close
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        close(server_fd);
        return EXIT_FAILURE;
    }
    
    // ορισμος ip που θα ακουει το socket
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; //δεχεται οποιαδηποτε ip
    addr.sin_port = htons(port);
    
    // Bind socket to address with error checking
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        close(server_fd);
        return EXIT_FAILURE;
    }
    
    //κατασταση ακροασης εως και 10 ταυτοχρονες συνδεσεις
    listen(server_fd, 10);

    while (1) //περιμενουμε συνδεση,αν οχι αγνοουμε και συνεχιζουμε
    {
        int client = accept(server_fd, NULL, NULL); 
        if (client < 0)
            continue;
            
        // Read command word by word: cmd, filename, size, then data
        char cmd[16] = {0};
        char arg[MAX_LINE] = {0};
        ssize_t size = -2; // -2 means not set yet
        
        // Read command until whitespace
        ssize_t cmd_len = 0;
        while (cmd_len < sizeof(cmd) - 1) {
            char c;
            ssize_t r = read(client, &c, 1);
            if (r <= 0) {
                close(client);
                goto next_client;
            }
            if (c == ' ') {
                break;
            }
            if (c == '\n' || c == '\r') {
                // Command with no arguments (shouldn't happen with our protocol)
                break;
            }
            cmd[cmd_len++] = c;
        }
        cmd[cmd_len] = '\0';
        
        // Read filename/directory until whitespace
        ssize_t arg_len = 0;
        while (arg_len < sizeof(arg) - 1) {
            char c;
            ssize_t r = read(client, &c, 1);
            if (r <= 0) {
                close(client);
                goto next_client;
            }
            if (c == ' ') {
                break;
            }
            if (c == '\n' || c == '\r') {
                // End of command line (LIST and PULL commands)
                break;
            }
            arg[arg_len++] = c;
        }
        arg[arg_len] = '\0';
        
        // For PUSH commands, read the size
        if (strcmp(cmd, "PUSH") == 0) {
            char size_str[32] = {0};
            ssize_t size_len = 0;
            while (size_len < sizeof(size_str) - 1) {
                char c;
                ssize_t r = read(client, &c, 1);
                if (r <= 0) {
                    close(client);
                    goto next_client;
                }
                if (c == ' ') {
                    break;
                }
                if (c == '\n' || c == '\r') {
                    break;
                }
                size_str[size_len++] = c;
            }
            size_str[size_len] = '\0';
            size = atol(size_str);
        }
        
        if (strlen(cmd) == 0) {
            close(client);
            continue;
        }

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
            if (size == -1) 
            {
                // Create/truncate the file to start fresh
                int init_fd = open(arg, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (init_fd != -1) {
                    close(init_fd);  // Close immediately after creating
                }
            }
            else if (size == 0)
            {
                // File is already complete, nothing to do
            }
            else if (size > 0)
            {
                // Open file in append mode for this specific chunk
                int chunk_fd = open(arg, O_WRONLY | O_APPEND);
                if (chunk_fd == -1) {
                    // Still need to consume the data from socket to avoid protocol issues
                    char buf[BUFSIZE];
                    ssize_t consumed = 0;
                    while (consumed < size) {
                        ssize_t r = read(client, buf, size - consumed);
                        if (r <= 0) break;
                        consumed += r;
                    }
                } else {
                    char buf[BUFSIZE];
                    ssize_t received = 0;
                    while (received < size)
                    {
                        ssize_t r = read(client, buf, size - received);
                        if (r <= 0) {
                            break;
                        }
                        write(chunk_fd, buf, r);
                        received += r;
                    }
                    close(chunk_fd);  // Close after writing this chunk
                }
            }
        }

        close(client);
        next_client:;
    }

    close(server_fd);
    return 0;
}
