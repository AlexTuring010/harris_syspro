Ονοματεπώνυμο : Χάραλαμπος Ραφαήλ Δημητρίου 
ΑΜ : 1115201900354


Η εργασία υλοποιεί ένα Δικτυακό Σύστημα Συγχρονισμού Αρχείων (NFS) και αποτελείται από 3 αρχεία:
- nfs_manager: Ο manager οργανώνει τον συγχρονισμό αρχείων, εκκινεί worker threads και χειρίζεται εντολές add, cancel, shutdown από το nfs_console καθώς και επικοινωνεί με nfs_client για PULL/PUSH και καταγράφει logs με timestamps.
- nfs_client: Ακούει σε TCP socket και εξυπηρετεί εντολές LIST, PULL, PUSH για τοπικά αρχεία καθώς και υλοποιεί μεταφορά αρχείων σε chunks με system calls (open/read/write/close).
- nfs_console: Παρέχει διεπαφή χρήστη για την αποστολή εντολών στο nfs_manager μέσω socket TCP και καταγράφει τις εντολές σε log και εμφανίζει τις αποκρίσεις του διαχειριστή.
- basics.c και basics.h: Περιέχει κοινές βοηθητικές συναρτήσεις και δομές δεδομένων όπως hash table και ουρά εργασιών και Υποστηρίζουν συγχρονισμό πρόσβασης (mutexes), timestamps και enqueue/dequeue.


Η επικοινωνία μεταξύ των υποσυστημάτων γίνεται με sockets, ενώ ο συγχρονισμός αρχείων υλοποιείται με worker threads, mutexes και condition variables. Όλες οι εντολές και αποτελέσματα καταγράφονται σε κατάλληλα log αρχεία.

---



Οδηγίες Εκτέλεσης:
αρχικά για μεταγλώττιση εκτελούμε:make clean && make

σε διαφορετικά terminal:
Εκκίνηση των nfs_client (source & target):
./nfs_client -p 5002 -d input_dir (1o terminal)
./nfs_client -p 5003 -d output_dir (2o terminal)

Εκκίνηση του nfs_manager(3o terminal):
./nfs_manager -l manager_logfile.txt -c config.txt -n 5 -p 5000 -b 10

Εκκίνηση του nfs_console(4o terminal):
./nfs_console -l console_log.txt -h 127.0.0.1 -p 5000

με παραδείγματα εντολών:
add input_dir@127.0.0.1:5002 output_dir@127.0.0.1:5003
cancel input_dir
shutdown

