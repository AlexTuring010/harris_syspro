#include "basics.h"

SyncEntry *sync_list = NULL;
SyncEntry *hash_table[HASH_SIZE] = {NULL};
queue_stats *queue_head = NULL;
queue_stats *queue_tail = NULL;

unsigned int hash_function(const char *input)// υπολογισμος hash value
{
    unsigned int hash_value = 5381;
    int current_char;

    while ((current_char = *input++))
    {
        hash_value = ((hash_value << 5) + hash_value) + current_char;
    }

    return hash_value % HASH_SIZE;
}

SyncEntry *find_node(int wd)//αναζητηση wd στο hash table
{
    for (int i = 0; i < HASH_SIZE; i++)
    {
        SyncEntry *current = hash_table[i];
        while (current)
        {
            if (current->wd == wd)
                return current;
            current = current->next;
        }
    }
    return NULL;
}

void insert_node(const char *source, const char *target)//προσθηκη νεου node στο hash table
{
    unsigned int idx = hash_function(source);
    SyncEntry *new_node = malloc(sizeof(SyncEntry));
    if (!new_node)
    {
        perror("malloc SyncEntry");
        exit(1);
    }
    strcpy(new_node->source, source);
    strcpy(new_node->target, target);
    new_node->active = 1;
    new_node->wd = -1;
    new_node->next = hash_table[idx];
    hash_table[idx] = new_node;
}

SyncEntry *find_node_source(const char *source)// αναζητηση source στο hash table
{ 
    unsigned int idx = hash_function(source);
    SyncEntry *current = hash_table[idx];
    while (current)
    {
        if (strcmp(current->source, source) == 0)
            return current;
        current = current->next;
    }
    return NULL;
}

void set_node_inactive(const char *source)//απενεργοποιηση node
{ 
    SyncEntry *node = find_node_source(source);
    if (node)
    {
        node->active = 0;
    }
}

void print_list()//εκτυπωση του hash table
{
    for (int i = 0; i < HASH_SIZE; i++)
    {
        SyncEntry *current = hash_table[i];
        while (current)
        {
            printf("Source: %s | Target: %s | Status: %s\n",
                   current->source, current->target,
                   current->active ? "ACTIVE" : "INACTIVE");
            current = current->next;
        }
    }
}

void free_list() //απελευθερωση μνημης του hash table
{
    for (int i = 0; i < HASH_SIZE; i++)
    {
        SyncEntry *current = hash_table[i];
        while (current)
        {
            SyncEntry *temp = current;
            current = current->next;
            free(temp);
        }
        hash_table[i] = NULL;
    }
}

void get_timestamp(char *buffer, size_t size) // current timestamp
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

void init_queue() // αρχικοποιηση ουρας queue
{
    queue_head = queue_tail = NULL;
}

void enqueue(const char *source, const char *target, const char *filename, const char *operation) // προσθηκη νεου node στην ουρα
{
    queue_stats *new_node = malloc(sizeof(queue_stats));
    if (!new_node)
    {
        perror("malloc queue_stats");
        exit(1);
    }
    strcpy(new_node->source, source);
    strcpy(new_node->target, target);
    strcpy(new_node->filename, filename);
    strcpy(new_node->operation, operation);
    new_node->next = NULL;

    if (!queue_tail)
    {
        queue_head = queue_tail = new_node;
    }
    else
    {
        queue_tail->next = new_node;
        queue_tail = new_node;
    }
}

queue_stats *dequeue()// αφαιρεση του πρωτου node απο την ουρα
{
    if (is_empty())
        return NULL;
    queue_stats *temp = queue_head;
    queue_head = queue_head->next;
    if (!queue_head)
        queue_tail = NULL;
    return temp;
}

int is_empty() //ελεγχος αν η ουρα ειναι κενη
{
    return queue_head == NULL;
}