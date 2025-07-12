#include "basics.h"

void remove_newline(char *str)
{
    size_t len = strlen(str);
    if (len > 0 && str[len - 1] == '\n')
        str[len - 1] = '\0';
}

void log_command(FILE *log_file, const char *command)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));
    fprintf(log_file, "[%s] Command %s\n", timestamp, command);
    fflush(log_file);
}

void log_response(FILE *log_file, const char *response)
{
    fprintf(log_file, "%s", response);
    fflush(log_file);
}

int connect_to_manager(const char *host, int port) //δημιουργια tcp socker,μετατροπη hostname σε ip address και συνδεση με τον manager
{
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    if ((server = gethostbyname(host)) == NULL) {
        fprintf(stderr, "ERROR, no such host as %s\n", host);
        exit(EXIT_FAILURE);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr, server->h_addr_list[0], server->h_length);
    serv_addr.sin_port = htons(port);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

int main(int argc, char *argv[])
{
    if (argc != 7 || strcmp(argv[1], "-l") != 0 || strcmp(argv[3], "-h") != 0 || strcmp(argv[5], "-p") != 0) {
        fprintf(stderr, "Usage: %s -l <console_logfile> -h <host_IP> -p <host_port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *log_path = argv[2];
    char *host_ip = argv[4];
    int port = atoi(argv[6]);

    FILE *log_file = fopen(log_path, "a");
    if (!log_file)
    {
        perror("fopen console log");
        return EXIT_FAILURE;
    }


    char command[MAX_LINE];
    char response[RESPONSE_SIZE];

    while (1) 
{
    printf("> ");
    fflush(stdout);

    if (!fgets(command, sizeof(command), stdin))
        break;

    remove_newline(command);
    log_command(log_file, command);

    int sockfd = connect_to_manager(host_ip, port);
    if (sockfd < 0) {
        fprintf(stderr, "Failed to connect to manager\n");
        continue;
    }

    dprintf(sockfd, "%s\n", command);

    ssize_t n = read(sockfd, response, sizeof(response) - 1);
    if (n > 0) {
        response[n] = '\0';
        printf("%s", response);
        log_response(log_file, response);
    } else {
        perror("read");
    }

    close(sockfd); // ➤ Κλείσιμο της σύνδεσης

    if (strncmp(command, "shutdown", 8) == 0)
        break;
}
    fclose(log_file);
    return EXIT_SUCCESS;
}
