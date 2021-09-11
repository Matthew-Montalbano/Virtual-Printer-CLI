#ifndef MY_IMPRIMER_H
#define MY_IMPRIMER_H

typedef struct printer {
	int id;
	char *name;
	FILE_TYPE *type;
	PRINTER_STATUS status;
} PRINTER;

typedef struct job {
	int id;
	FILE_TYPE *type;
	JOB_STATUS status;
	int eligible;
	char *file;
	PRINTER *selected_printer;
	CONVERSION **conversion_path;
} JOB;


void free_memory();
void free_printers();
void free_jobs();
void free_job(JOB *job);





int parse_command(char *command, FILE *in, FILE *out);

void readline_callback();
JOB *find_job_from_pid(int pid);
void dequeue_finished_jobs();
void delete_job(JOB *job);

void sigchld_handler();

int read_commands_from_file(FILE *in, FILE *out);
int read_commands_from_stdin(FILE *in, FILE *out);

void process_type(char *command);

int process_arguments(char *args, char **args_arr, int min_args, int max_args);

void process_printer(char *command);
int find_free_printer_id();
int valid_printer_name(char *name);
void allocate_and_save_printer(int id, char *name, FILE_TYPE *type);



void process_conversion(char *command);
int count_args(char *str);
void copy_array(char **source, char **dest, int num_elements);


void process_print(char *command, FILE *in, FILE *out);
int get_all_printers();
int get_eligible_printers(char **names);
int start_print_job(char *name, FILE_TYPE *type, int bitmap);
int find_free_job_id();


void cancel_job(char *command);
void pause_job(char *command);
void resume_job(char *command);




void change_printer_status(char *command, PRINTER_STATUS status);
PRINTER *find_printer(char *name);


void display_printers(FILE *out);
void display_jobs(FILE *out);



void run_available_jobs();
PRINTER *find_printer_for_job(JOB *job);
void run_job(JOB *job, PRINTER *printer);
int unblock_sigterm_sigpipe();
int count_links_in_conversion_path(CONVERSION **path);
void print_no_conversion(char *filename, int printer_descriptor);
void run_conversion_pipeline(char *filename, int printer_descriptor, CONVERSION **conversion_path);
int reap_children();
void update_running_job_statuses(JOB *job, PRINTER *printer, CONVERSION **pipeline, int pid);
void get_command_names(CONVERSION **pipeline, char **command_names);






#endif