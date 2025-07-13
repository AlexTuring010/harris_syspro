// client
#include "basics.h"

job_t job_buffer[1024];
int job_count = 0, job_in = 0, job_out = 0;

pthread_mutex_t job_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t job_not_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t job_not_full = PTHREAD_COND_INITIALIZER;

int active_workers = 0;
int worker_limit = 5;

pthread_t workers[1024];

FILE *log_file;

//για τα warnings στο makefile
static void safe_snprintf(char *buffer, size_t bufsize, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, bufsize, fmt, args);
    buffer[bufsize - 1] = '\0'; // ensure null-termination
    va_end(args);
}

void log_event(const char *message)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));
    fprintf(log_file, "[%s] %s\n", timestamp, message);
    fflush(log_file);
    // Also print to screen
    printf("[%s] %s\n", timestamp, message);
    fflush(stdout);
}

void sigchld_handler(int signo) {
    (void)signo;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

void enqueue_job(job_t job)
{ // προσθηκη νεου job στην ουρα
    pthread_mutex_lock(&job_mutex);
    while (job_count == 1024)
        pthread_cond_wait(&job_not_full, &job_mutex);
    job_buffer[job_in] = job;
    job_in = (job_in + 1) % 1024;
    job_count++;
    pthread_cond_signal(&job_not_empty);
    pthread_mutex_unlock(&job_mutex);
}

job_t dequeue_job()
{ // αφαιρεση job απο την ουρα
    pthread_mutex_lock(&job_mutex);
    while (job_count == 0 || active_workers >= worker_limit) // προσθηκη με το οριο worker
        pthread_cond_wait(&job_not_empty, &job_mutex);

    job_t job = job_buffer[job_out];
    job_out = (job_out + 1) % 1024;
    job_count--;

    pthread_cond_signal(&job_not_full);
    pthread_mutex_unlock(&job_mutex);
    return job;
}

void *worker_thread(void *arg)
{ // thread που εκτελει τα jobs
    while (1)
    {
        job_t job = dequeue_job();

        pthread_mutex_lock(&job_mutex);
        active_workers++;
        pthread_mutex_unlock(&job_mutex);

        // συνδεση με source client
        int source_sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in source_addr;
        memset(&source_addr, 0, sizeof(source_addr));
        source_addr.sin_family = AF_INET;
        source_addr.sin_port = htons(job.source_port);
        inet_pton(AF_INET, job.source_host, &source_addr.sin_addr);

        if (connect(source_sock, (struct sockaddr *)&source_addr, sizeof(source_addr)) < 0)
        {
            char msg[1024];
            safe_snprintf(msg, sizeof(msg), "[%s] [%s] [%s] [PULL] [ERROR] [Failed to connect to source: %s]",
                     job.source_dir, job.target_dir, job.filename, strerror(errno));
            log_event(msg);
            pthread_mutex_lock(&job_mutex);
            active_workers--;
            pthread_mutex_unlock(&job_mutex);
            continue;
        }
        // στελνουμε εντολη PULL
        char pull_cmd[2048];
        safe_snprintf(pull_cmd, sizeof(pull_cmd), "PULL %s/%s\n", job.source_dir, job.filename);
        dprintf(source_sock, "%s", pull_cmd);
        //διαβαζουμε απαντηση
        char meta[64];
        ssize_t meta_len = 0;
        
        // Read metadata character by character until we hit a space
        while (meta_len < sizeof(meta) - 1) {
            char c;
            ssize_t r = read(source_sock, &c, 1);
            if (r <= 0) {
                close(source_sock);
                pthread_mutex_lock(&job_mutex);
                active_workers--;
                pthread_mutex_unlock(&job_mutex);
                goto next_job;
            }
            meta[meta_len++] = c;
            if (c == ' ') {
                break;
            }
        }
        meta[meta_len] = '\0';

        long filesize;
        sscanf(meta, "%ld", &filesize);

        if (filesize == -1)
        {
            char *err_msg = strchr(meta, ' ');
            char msg[2048];
            safe_snprintf(msg, sizeof(msg), "[%s/%s@%s:%d] [%s/%s@%s:%d] [%ld] [PULL] [ERROR] [%s]",
                     job.source_dir, job.filename, job.source_host, job.source_port,
                     job.target_dir, job.filename, job.target_host, job.target_port,
                     pthread_self(), err_msg ? err_msg + 1 : "Unknown error");
            log_event(msg);
            close(source_sock);
            pthread_mutex_lock(&job_mutex); //μειωνουμε τον ενεργο αριθμο εργατων
            active_workers--;
            pthread_mutex_unlock(&job_mutex); // επιστρεφουμε στην ουρα για νεο job
            continue;
        }

        // First connection: Send PUSH -1 to initialize file
        int target_sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in target_addr;
        memset(&target_addr, 0, sizeof(target_addr));
        target_addr.sin_family = AF_INET;
        target_addr.sin_port = htons(job.target_port);
        inet_pton(AF_INET, job.target_host, &target_addr.sin_addr);

        if (connect(target_sock, (struct sockaddr *)&target_addr, sizeof(target_addr)) < 0)
        {
            char msg[256];
            safe_snprintf(msg, sizeof(msg), "[%s/%s@%s:%d] [%s/%s@%s:%d] [%ld] [PUSH] [ERROR] [Failed to connect to target: %s]",
                     job.source_dir, job.filename, job.source_host, job.source_port,
                     job.target_dir, job.filename, job.target_host, job.target_port,
                     pthread_self(), strerror(errno));
            log_event(msg);
            close(source_sock);
            pthread_mutex_lock(&job_mutex);
            active_workers--;
            pthread_mutex_unlock(&job_mutex);
            continue;
        }

        // ειδοποιουμε για write
        char push_cmd[2048];
        safe_snprintf(push_cmd, sizeof(push_cmd), "PUSH %s/%s -1 dummy\n", job.target_dir, job.filename);
        dprintf(target_sock, "%s", push_cmd);
        
        close(target_sock);  // Close after first command

        // διαβαζουμε το υπολοιπο του αρχειου και το στελνουμε σε chunks
        char buffer[BUFSIZE];
        ssize_t total = 0;
        ssize_t n;
        
        while ((n = read(source_sock, buffer, BUFSIZE)) > 0)
        {
            total += n;
            
            // New connection for each chunk
            target_sock = socket(AF_INET, SOCK_STREAM, 0);
            if (connect(target_sock, (struct sockaddr *)&target_addr, sizeof(target_addr)) < 0)
            {
                char msg[256];
                safe_snprintf(msg, sizeof(msg), "[%s/%s@%s:%d] [%s/%s@%s:%d] [%ld] [PUSH] [ERROR] [Failed to reconnect to target: %s]",
                         job.source_dir, job.filename, job.source_host, job.source_port,
                         job.target_dir, job.filename, job.target_host, job.target_port,
                         pthread_self(), strerror(errno));
                log_event(msg);
                break;
            }
            
            char push_data[2048];
            safe_snprintf(push_data, sizeof(push_data), "PUSH %s/%s %ld ", job.target_dir, job.filename, n);
            dprintf(target_sock, "%s", push_data);
            write(target_sock, buffer, n);
            
            shutdown(target_sock, SHUT_WR);
            close(target_sock);  // Close after each chunk
        }

        // Final connection: Send PUSH 0 to finalize file
        target_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (connect(target_sock, (struct sockaddr *)&target_addr, sizeof(target_addr)) < 0)
        {
            char msg[256];
            safe_snprintf(msg, sizeof(msg), "[%s/%s@%s:%d] [%s/%s@%s:%d] [%ld] [PUSH] [ERROR] [Failed to connect for finalization: %s]",
                     job.source_dir, job.filename, job.source_host, job.source_port,
                     job.target_dir, job.filename, job.target_host, job.target_port,
                     pthread_self(), strerror(errno));
            log_event(msg);
        } else {
            safe_snprintf(push_cmd, sizeof(push_cmd), "PUSH %s/%s 0 end\n", job.target_dir, job.filename);
            dprintf(target_sock, "%s", push_cmd);
            close(target_sock);  // Close after final command
        }

        //pull και push στο log
        char msg1[2048], msg2[2048];
        safe_snprintf(msg1, sizeof(msg1), "[%s/%s@%s:%d] [%s/%s@%s:%d] [%ld] [PULL] [SUCCESS] [%ld bytes pulled]",
                 job.source_dir, job.filename, job.source_host, job.source_port,
                 job.target_dir, job.filename, job.target_host, job.target_port,
                 pthread_self(), total);
        safe_snprintf(msg2, sizeof(msg2), "[%s/%s@%s:%d] [%s/%s@%s:%d] [%ld] [PUSH] [SUCCESS] [%ld bytes pushed]",
                 job.source_dir, job.filename, job.source_host, job.source_port,
                 job.target_dir, job.filename, job.target_host, job.target_port,
                 pthread_self(), total);

        log_event(msg1);
        log_event(msg2);

        close(source_sock);

        pthread_mutex_lock(&job_mutex);
        active_workers--;
        pthread_mutex_unlock(&job_mutex);
        next_job:;
    }

    return NULL;
}

void parse_config(const char *config_path)
{ // αναγνωση του config file
    FILE *file = fopen(config_path, "r");
    if (!file)
    {
        perror("fopen config_file");
        exit(EXIT_FAILURE);
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), file))
    {                                              // διαβαζουμε γραμμη προς γραμμη
        char src_dir[MAX_LINE], tgt_dir[MAX_LINE]; // source και target directories
        char src_host[64], tgt_host[64];
        int src_port, tgt_port;
        // αν η γραμμη εχει την σωστη μορφη
        if (sscanf(line, "%[^@]@%[^:]:%d %[^@]@%[^:]:%d", src_dir, src_host, &src_port, tgt_dir, tgt_host, &tgt_port) == 6)
        {
            insert_node(src_dir, tgt_dir);
            // δημιουργια συνδεσης με source για LIST
            int list_sock = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in src_addr;
            memset(&src_addr, 0, sizeof(src_addr));
            src_addr.sin_family = AF_INET;
            src_addr.sin_port = htons(src_port);
            inet_pton(AF_INET, src_host, &src_addr.sin_addr);

            if (connect(list_sock, (struct sockaddr *)&src_addr, sizeof(src_addr)) < 0)
            {
                char msg[256];
                safe_snprintf(msg, sizeof(msg), "LIST connection failed to %s:%d — %s", src_host, src_port, strerror(errno));
                log_event(msg);
                continue;
            }

            // στελνουμε την εντολη LIST
            char list_cmd[2048];
            safe_snprintf(list_cmd, sizeof(list_cmd), "LIST %s\n", src_dir);
            dprintf(list_sock, "%s", list_cmd);

            // παιρνουμε λιστα αρχειων
            char filename[MAX_LINE];
            char buffer[4096];
            int buf_pos = 0, buf_len = 0;
            
            while (1) {
                // Read line from socket buffer
                int line_pos = 0;
                while (line_pos < MAX_LINE - 1) {
                    if (buf_pos >= buf_len) {
                        buf_len = read(list_sock, buffer, sizeof(buffer));
                        if (buf_len <= 0) break;
                        buf_pos = 0;
                    }
                    char c = buffer[buf_pos++];
                    if (c == '\n') {
                        filename[line_pos] = '\0';
                        break;
                    }
                    filename[line_pos++] = c;
                }
                if (line_pos == 0) break;
                
                if (strcmp(filename, ".") == 0) break;

                job_t job;
                safe_snprintf(job.source_dir, sizeof(job.source_dir), "%s", src_dir);
                safe_snprintf(job.source_host, sizeof(job.source_host), "%s", src_host);
                job.source_port = src_port;

                safe_snprintf(job.target_dir, sizeof(job.target_dir), "%s", tgt_dir);
                safe_snprintf(job.target_host, sizeof(job.target_host), "%s", tgt_host);
                job.target_port = tgt_port;

                safe_snprintf(job.filename, sizeof(job.filename), "%s", filename);
                SyncEntry *entry = find_node_source(job.source_dir);
                if (!entry || entry->active)
                {
                    enqueue_job(job);
                    char log_msg[4096];
                    safe_snprintf(log_msg, sizeof(log_msg), "Added file: %s/%s@%s:%d -> %s/%s@%s:%d",
                             src_dir, filename, src_host, src_port,
                             tgt_dir, filename, tgt_host, tgt_port);
                    log_event(log_msg);
                }
                else
                {
                    char msg[MAX_LINE];
                    safe_snprintf(msg, sizeof(msg), "Skipped inactive source: %s/%s", job.source_dir, job.filename);
                    log_event(msg); // optional
                }
            }
            close(list_sock);
        }
    }
    fclose(file);
}

int main(int argc, char *argv[])
{
    if (argc != 11 || strcmp(argv[1], "-l") != 0 || strcmp(argv[3], "-c") != 0 ||
        strcmp(argv[5], "-n") != 0 || strcmp(argv[7], "-p") != 0 || strcmp(argv[9], "-b") != 0)
    {
        fprintf(stderr, "Usage: %s -l <logfile> -c <config_file> -n <worker_limit> -p <port> -b <bufferSize>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *log_path = argv[2];
    char *config_path = argv[4];
    worker_limit = atoi(argv[6]);
    int port = atoi(argv[8]);
    int buffer_size = atoi(argv[10]);

    if (buffer_size <= 0)
    {
        fprintf(stderr, "Buffer size must be > 0\n");
        exit(EXIT_FAILURE);
    }

    log_file = fopen(log_path, "w"); //ανοιγμα του log file
    if (!log_file)
    {
        perror("fopen log");
        exit(EXIT_FAILURE);
    }

    init_queue();
    signal(SIGCHLD, sigchld_handler);

    for (int i = 0; i < worker_limit; ++i)
        pthread_create(&workers[i], NULL, worker_thread, NULL);
    parse_config(config_path); // οργανωση των jobs απο το config file στην ουρα μεσω threads και mutex για την ασφαλη προσβαση
    // επικοινωνια με τον console
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Enable SO_REUSEADDR to allow port reuse immediately after close
    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    if (listen(server_sock, 10) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    log_event("Manager ready to accept console connections.");

    while (1)
    {
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0)
        {
            perror("accept");
            continue;
        }

        char buffer[MAX_LINE] = {0};
        ssize_t n = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            if (n == 0)
                fprintf(stderr, "Client disconnected\n");
            else
                perror("recv");
            
            close(client_sock);
            continue;
        }

        buffer[n] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';
        // καταγραφη εντολης
        char log_msg[MAX_LINE + 64];
        safe_snprintf(log_msg, sizeof(log_msg), "Received command: %s", buffer);
        log_event(log_msg);
        //διαδικασια
        char command[MAX_LINE], src[MAX_LINE], tgt[MAX_LINE];
        int matched = sscanf(buffer, "%s %s %s", command, src, tgt);

        if (matched == 3 && strcmp(command, "add") == 0)
        {
            char src_dir[MAX_LINE], tgt_dir[MAX_LINE];
            char src_host[64], tgt_host[64];
            int src_port, tgt_port;

            // source@host:port και target@host:port
            if (sscanf(src, "%[^@]@%[^:]:%d", src_dir, src_host, &src_port) != 3 ||
                sscanf(tgt, "%[^@]@%[^:]:%d", tgt_dir, tgt_host, &tgt_port) != 3)
            {
                SyncEntry *entry = find_node_source(src_dir);
                if (entry && entry->active) {
                    dprintf(client_sock, "[%s] Already in queue: %s@%s:%d\n", __TIME__, src_dir, src_host, src_port);
                    close(client_sock);
                    continue;
                }
                insert_node(src_dir, tgt_dir);
                dprintf(client_sock, "[%s] Invalid directories: %s %s\n", __TIME__, src, tgt);
                close(client_sock);
                continue;
            }

            //list στο source
            int sockfd = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in serv_addr;
            struct hostent *server = gethostbyname(src_host);
            if (!server)
            {
                char timestamp[64];
                get_timestamp(timestamp, sizeof(timestamp));
                dprintf(client_sock, "[%s] Cannot resolve host: %s\n", timestamp, src_host);
                close(client_sock);
                continue;
            }

            memset(&serv_addr, 0, sizeof(serv_addr));
            serv_addr.sin_family = AF_INET;
            memcpy(&serv_addr.sin_addr, server->h_addr_list[0], server->h_length);
            serv_addr.sin_port = htons(src_port);

            if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
            {
                char timestamp[64];
                get_timestamp(timestamp, sizeof(timestamp));
                dprintf(client_sock, "[%s] Cannot connect to source: %s:%d\n", timestamp, src_host, src_port);
                close(sockfd);
                close(client_sock);
                continue;
            }

            // Στειλε εντολη list
            dprintf(sockfd, "LIST %s\n", src_dir);

            char line[MAX_LINE];
            while (1)
            {
                ssize_t n = read(sockfd, line, sizeof(line) - 1);
                if (n <= 0)
                    break;
                line[n] = '\0';

                char *file = strtok(line, "\n");
                while (file)
                {
                    if (strcmp(file, ".") == 0)
                        break;

                    //enqueue job για καθε αρχειο
                    job_t job;
                    safe_snprintf(job.source_dir, sizeof(job.source_dir), "%s", src_dir);
                    safe_snprintf(job.target_dir, sizeof(job.target_dir), "%s", tgt_dir);
                    safe_snprintf(job.source_host, sizeof(job.source_host), "%s", src_host);
                    safe_snprintf(job.target_host, sizeof(job.target_host), "%s", tgt_host);
                    job.source_port = src_port;
                    job.target_port = tgt_port;
                    safe_snprintf(job.filename, sizeof(job.filename), "%s", file);

                    enqueue_job(job);

                    // καταγραφη σε κονσολα και log
                    char msg[MAX_LINE * 2];
                    safe_snprintf(msg, sizeof(msg), "Added file: %s/%s@%s:%d -> %s/%s@%s:%d",
                             src_dir, file, src_host, src_port,
                             tgt_dir, file, tgt_host, tgt_port);
                    char timestamp[64];
                    get_timestamp(timestamp, sizeof(timestamp));
                    dprintf(client_sock, "[%s] %s\n", timestamp, msg);
                    log_event(msg);

                    file = strtok(NULL, "\n");
                }
            }

            close(sockfd);
        } 
        else if (matched == 2 && strcmp(command, "cancel") == 0)
        {
            set_node_inactive(src);
            char msg[256];
            safe_snprintf(msg, sizeof(msg), "synchronization stopped for %s", src);
            char timestamp[64];
            get_timestamp(timestamp, sizeof(timestamp));
            dprintf(client_sock, "[%s] %s\n", timestamp, msg);
            log_event(msg);
        }
        else if (strstr(buffer, "shutdown") == buffer)
        {
            char timestamp[64];
            get_timestamp(timestamp, sizeof(timestamp));

            dprintf(client_sock, "[%s] shutting down manager...\n", timestamp);
            log_event("shutting down manager...");

            dprintf(client_sock, "[%s] waiting for all active workers to finish.\n", timestamp);
            dprintf(client_sock, "[%s] processing remaining queued tasks.\n", timestamp);

            for (int i = 0; i < worker_limit; ++i)
                pthread_cancel(workers[i]);

            dprintf(client_sock, "[%s] manager shutdown complete.\n", timestamp);
            log_event("manager shutdown complete.");

            close(client_sock);
            fclose(log_file);
            exit(0); 
        }
        else if (strstr(buffer, "print") == buffer)
        {
            char timestamp[64];
            get_timestamp(timestamp, sizeof(timestamp));
            dprintf(client_sock, "[%s] current synchronization jobs:\n", timestamp);
            print_list();
            log_event("printed current synchronization jobs.");
        }
        else if (strstr(buffer, "status") == buffer)
        {
            char timestamp[64];
            get_timestamp(timestamp, sizeof(timestamp));
            dprintf(client_sock, "[%s] active workers: %d\n", timestamp, active_workers);
            log_event("reported active workers status.");
        }

        else
        {
            char timestamp[64];
            get_timestamp(timestamp, sizeof(timestamp));
            dprintf(client_sock, "[%s] unknown command\n", timestamp); // parsing και διαχειριση εντολων add, cancel, shutdown
        }

        close(client_sock);
    }

    close(server_sock);

    fclose(log_file);
    return 0;
}
