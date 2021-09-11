/*
 * Imprimer: Command-line interface
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>


#include "imprimer.h"
#include "conversions.h"
#include "sf_readline.h"
#include "my_imprimer.h"
#include "debug.h"

static PRINTER *printers[MAX_PRINTERS];
static JOB *jobs[MAX_JOBS];
static int job_id_to_pid[MAX_JOBS];
static time_t times_elapsed[MAX_JOBS];
sig_atomic_t volatile job_finished;
//char *printer_status_names[3] = {"disabled", "idle", "busy"};
//char *job_status_names[6] = {"created", "running", "paused", "finished", "aborted", "deleted"};

int run_cli(FILE *in, FILE *out)
{
	int exit_code;
	signal(SIGCHLD, sigchld_handler);
	sf_set_readline_signal_hook(readline_callback);
	if (in == NULL) {
		exit_code = -1;
	} else if (in == stdin) {
		exit_code = read_commands_from_stdin(in, out);
	} else {
    	exit_code = read_commands_from_file(in, out);
	}
    return exit_code;
}

void readline_callback() {
	if (job_finished) {
		job_finished = 0;
		int status, pid;
		JOB *job;
		while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
			job = find_job_from_pid(pid);
			if (WIFEXITED(status)) {
				if (WEXITSTATUS(status) != 0) {
					job->status = JOB_ABORTED;
					sf_job_aborted(job->id, WEXITSTATUS(status));
				} else {
					job->status = JOB_FINISHED;
					sf_job_finished(job->id, WEXITSTATUS(status));
				}
				sf_job_status(job->id, job->status);
				if (job->selected_printer->status != PRINTER_DISABLED) {
					job->selected_printer->status = PRINTER_IDLE;
					sf_printer_status(job->selected_printer->name, PRINTER_IDLE);
				}
				job_id_to_pid[job->id] = 0;
				times_elapsed[job->id] = time(NULL);
			} else if (WIFSTOPPED(status)) {
				job->status = JOB_PAUSED;
				sf_job_status(job->id, JOB_PAUSED);
			} else if (WIFCONTINUED(status)) {
				job->status = JOB_RUNNING;
				sf_job_status(job->id, JOB_RUNNING);
			} else if (WIFSIGNALED(status)) {
				job->status = JOB_ABORTED;
				sf_job_aborted(job->id, WTERMSIG(status));
				sf_job_status(job->id, JOB_ABORTED);
				if (job->selected_printer->status != PRINTER_DISABLED) {
					job->selected_printer->status = PRINTER_IDLE;
					sf_printer_status(job->selected_printer->name, PRINTER_IDLE);
				};
				job_id_to_pid[job->id] = 0;
				times_elapsed[job->id] = time(NULL);
			}
		}
	}
	run_available_jobs();
}

JOB *find_job_from_pid(int pid) {
	for (int i = 0; i < MAX_JOBS; i++) {
		if (job_id_to_pid[i] == pid) {
			return jobs[i];
		}
	}
	return NULL;
}

void dequeue_finished_jobs() {
	JOB *job;
	int seconds;
	for (int i = 0; i < MAX_JOBS; i++) {
		job = jobs[i];
		if (job != NULL && (job->status == JOB_FINISHED || job->status == JOB_ABORTED)) {
			seconds = time(NULL) - times_elapsed[i];
			if (seconds >= 10) {
				delete_job(job);
				jobs[i] = NULL;
			}
		}
	}
}

void delete_job(JOB *job) {
	job->status = JOB_DELETED;
	sf_job_deleted(job->id);
	sf_job_status(job->id, JOB_DELETED);
	free_job(job);
}



void sigchld_handler(int sig) {
	job_finished = 1;
}

int read_commands_from_file(FILE *in, FILE *out) {
	int string_size, res;
	size_t line_size = 100;
	char *line = malloc(sizeof(char) * line_size);
	while ((string_size = getline(&line, &line_size, in)) != -1) {
		readline_callback();
		if (line[string_size - 1] == '\n') {
			line[string_size - 1] = '\0';
		}
		dequeue_finished_jobs();
		res = parse_command(line, in, out);
		if (res == -1) {
			free_memory();
			free(line);
			return -1;
		}
	}
	free(line);
	return 0;
}

int read_commands_from_stdin(FILE *in, FILE *out) {
	char *input;
	int res = 0;
	char *prompt = (out == stdout ? "imp>" : "");
    while (res == 0) {
    	if ((input = sf_readline(prompt)) == NULL) {
    		break;
    	}
    	dequeue_finished_jobs();
    	res = parse_command(input, in, out);
    	free(input);
    }
    free_memory();
    return -1;
}


int parse_command(char *command, FILE *in, FILE *out) {
	char *token = strtok_r(command, " ", &command);
	if (token == NULL) {
		return 0;
	}
	if (strcmp(token, "help") == 0) {
		fprintf(out, "Available commands: help, quit, type, printer, conversion, printers, jobs, print, cancel, pause, resume, disable, enable\n");
		sf_cmd_ok();
	} else if (strcmp(token, "quit") == 0) {
		return -1;
	} else if (strcmp(token, "type") == 0) {
		process_type(command);
	} else if (strcmp(token, "printer") == 0) {
		process_printer(command);
	} else if (strcmp(token, "conversion") == 0) {
		process_conversion(command);
	} else if (strcmp(token, "printers") == 0) {
		display_printers(out);
	} else if (strcmp(token, "jobs") == 0) {
		display_jobs(out);
	} else if (strcmp(token, "print") == 0) {
		process_print(command, in, out);
	} else if (strcmp(token, "cancel") == 0) {
		cancel_job(command);
	} else if (strcmp(token, "pause") == 0) {
		pause_job(command);
	} else if (strcmp(token, "resume") == 0) {
		resume_job(command);
	} else if (strcmp(token, "disable") == 0) {
		change_printer_status(command, PRINTER_DISABLED);
	} else if (strcmp(token, "enable") == 0) {
		change_printer_status(command, PRINTER_IDLE);
	} else if (strcmp(token, "\0") == 0) {
		return -2;
	} else {
		sf_cmd_error("Invalid Command");
	}
	return 0;
}






void free_memory() {
	free_printers();
	free_jobs();
}

void free_printers() {
	for (int i = 0; i < MAX_PRINTERS; i++) {
		if (printers[i] != NULL) {
			free(printers[i]->name);
			free(printers[i]);
		}
	}
}

void free_jobs() {
	for (int i = 0; i < MAX_JOBS; i++) {
		if (jobs[i] != NULL) {
			free_job(jobs[i]);
		}
	}
}

void free_job(JOB *job) {
	free(job->file);
	if (job->conversion_path != NULL) {
		free(job->conversion_path);
	}
	free(job);
}






void process_type(char *command) {
	int expected_args = 1;
	char *args[expected_args];
	if (!process_arguments(command, args, expected_args, expected_args)) {
		sf_cmd_error("Incorrect number of args");
		return;
	}
	FILE_TYPE *file_type = define_type(args[0]);
	if (file_type == NULL) {
		sf_cmd_error("Could not create file type");
		return;
	}
	sf_cmd_ok();
}

int process_arguments(char *args, char **args_arr, int min_args, int max_args) {
	char *token;
	int count = 0;
	while ((token = strtok_r(args, " ", &args))) {
		count++;
		if (count > max_args) {
			return 0;
		}
		args_arr[count-1] = token;
	}
	if (count < min_args) {
		return 0;
	}
	return 1;
}





void process_printer(char *command) {
	int expected_args = 2;
	char *args[expected_args];
	if (!process_arguments(command, args, expected_args, expected_args)) {
		sf_cmd_error("Incorrect number of args");
		return;
	}
	int id = find_free_printer_id();
	if (id == -1) {
		sf_cmd_error("Max printers reached");
		return;
	}
	if (!valid_printer_name(args[0])) {
		sf_cmd_error("Printer name already used");
		return;
	}
	FILE_TYPE *type = find_type(args[1]);
	if (type == NULL) {
		sf_cmd_error("Invalid file type");
		return;
	}
	allocate_and_save_printer(id, args[0], type);
	sf_printer_defined(args[0], args[1]);
	sf_cmd_ok();
}

int find_free_printer_id() {
	for (int i = 0; i < MAX_PRINTERS; i++) {
		if (printers[i] == NULL) {
			return i;
		}
	}
	return -1;
}

int valid_printer_name(char *name) {
	for (int i = 0; i < MAX_PRINTERS; i++) {
		if (printers[i] != NULL && (strcmp(name, printers[i]->name) == 0)) {
			return 0;
		}
	}
	return 1;
}

void allocate_and_save_printer(int id, char *name, FILE_TYPE *type) {
	PRINTER *printer = malloc(sizeof(PRINTER));
	char *new_name = malloc(strlen(name) + 1);
	strcpy(new_name, name);
	printer->id = id;
	printer->name = new_name;
	printer->type = type;
	printer->status = PRINTER_DISABLED;
	printers[id] = printer;
}






void process_conversion(char *command) {
	int expected_args = count_args(command);
	char *args[expected_args];
	if (!process_arguments(command, args, 3, expected_args)) {
		sf_cmd_error("Incorrect number of args");
		return;
	}
	FILE_TYPE *type_one = find_type(args[0]);
	FILE_TYPE *type_two = find_type(args[1]);
	if (type_one == NULL || type_two == NULL) {
		sf_cmd_error("Invalid file type");
		return;
	}
	char *cmd_and_args[expected_args - 1];
	copy_array(args + 2, cmd_and_args, expected_args - 2);
	define_conversion(type_one->name, type_two->name, cmd_and_args);
	sf_cmd_ok();
}

int count_args(char *str) {
	char *p = str;
	int count = 0;
	while (*p) {
		if (*p == ' ') count++;
		p++;
	}
	count++;
	return count;
}

void copy_array(char **source, char **dest, int num_elements) {
	for (int i = 0; i < num_elements; i++) {
		dest[i] = *source;
		source++;
	}
	dest[num_elements] = '\0';
}




void display_printers(FILE *out) {
	PRINTER *printer;
	for (int i = 0; i < MAX_PRINTERS; i++) {
		if (printers[i] != NULL) {
			printer = printers[i];
			fprintf(out, "PRINTER: id=%d, name=%s, type=%s, status=%s\n", printer->id, printer->name, printer->type->name, printer_status_names[printer->status]);
		}
	}
	sf_cmd_ok();
}

void display_jobs(FILE *out) {
	JOB *job;
	for (int i = 0; i < MAX_JOBS; i++) {
		if (jobs[i] != NULL) {
			job = jobs[i];
			fprintf(out, "JOB: id=%d, type=%s, status=%s, eligible=%08x, file=%s\n", job->id, job->type->name, job_status_names[job->status], job->eligible, job->file);
		}
	}
	sf_cmd_ok();
}





void process_print(char *command, FILE *in, FILE *out) {
	int expected_args = count_args(command);
	char *args[expected_args];
	if (!process_arguments(command, args, 1, expected_args)) {
		sf_cmd_error("Incorrect number of args");
		return;
	}
	FILE_TYPE *type = infer_file_type(args[0]);
	if (type == NULL) {
		sf_cmd_error("Invalid file type");
		return;
	}
	char *eligible_printer_names[expected_args];
	copy_array(args + 1, eligible_printer_names, expected_args - 1);
	int eligible_bitmap = ~0;
	if (expected_args == 1) {
		eligible_bitmap = get_all_printers();
	} else {
		if (!(eligible_bitmap = get_eligible_printers(eligible_printer_names))) {
			sf_cmd_error("Invalid printer name(s)");
			return;
		}
	}
	if (!start_print_job(args[0], type, eligible_bitmap)) {
		sf_cmd_error("Job limit reached");
		return;
	}
	sf_cmd_ok();
}

int count_printers() {
	int count = 0;
	for (int i = 0; i < MAX_PRINTERS; i++) {
		if (printers[i] != NULL) {
			count++;
		}
	}
	return count;
}

int get_all_printers() {
	int bitmap = 0;
	for (int i = 0; i < MAX_PRINTERS; i++) {
		if (printers[i] != NULL) {
			bitmap |= (1 << printers[i]->id);
		}
	}
	return bitmap;
}

int get_eligible_printers(char **names) {
	int i = 0;
	int bitmap = 0;
	PRINTER *printer;
	while (names[i] != NULL) {
		printer = find_printer(names[i]);
		if (printer == NULL) {
			return 0;
		}
		bitmap |= (1 << printer->id);
		i++;
	}
	return bitmap;
}

int start_print_job(char *name, FILE_TYPE *type, int bitmap) {
	int id = find_free_job_id();
	if (id == -1) {
		return 0;
	}
	JOB *job = malloc(sizeof(JOB));
	char *new_name = malloc(strlen(name) + 1);
	strcpy(new_name, name);
	job->id = id;
	job->type = type;
	job->status = JOB_CREATED;
	job->eligible = bitmap;
	job->file = new_name;
	job->selected_printer = NULL;
	job->conversion_path = NULL;
	jobs[id] = job;
	sf_job_created(id, new_name, type->name);
	return 1;
}

int find_free_job_id() {
	for (int i = 0; i < MAX_JOBS; i++) {
		if (jobs[i] == NULL) {
			return i;
		}
	}
	return -1;
}



void cancel_job(char *command) {
	int job_num;
	int expected_args = 1;
	char *args[expected_args];
	if (!process_arguments(command, args, expected_args, expected_args)) {
		sf_cmd_error("Incorrect number of args");
		return;
	}
	if (sscanf(args[0], "%d", &job_num) == -1 || job_num < 0 || job_num >= MAX_JOBS || jobs[job_num] == NULL) {
		sf_cmd_error("Not a valid job number");
		return;
	}
	JOB *job = jobs[job_num];
	int pid = job_id_to_pid[job_num];
	if (pid != 0) {
		if (killpg(pid, SIGTERM) == -1) {
			sf_cmd_error("Job could not be cancelled");
			return;
		}
		if (job->status == JOB_PAUSED && killpg(pid, SIGCONT) == -1) {
			sf_cmd_error("Job could not be cancelled (Could not continue paused process)");
			return;
		}
	} else if (job->status == JOB_CREATED) {
		job->status = JOB_ABORTED;
		sf_job_aborted(job->id, 0);
		sf_job_status(job->id, JOB_ABORTED);
		times_elapsed[job->id] = time(NULL);
	} else {
		sf_cmd_error("Job already finished/aborted");
		return;
	}
	sf_cmd_ok();
}


void pause_job(char *command) {
	int job_num;
	int expected_args = 1;
	char *args[expected_args];
	if (!process_arguments(command, args, expected_args, expected_args)) {
		sf_cmd_error("Incorrect number of args");
		return;
	}
	if (sscanf(args[0], "%d", &job_num) == -1 || job_id_to_pid[job_num] == 0) {
		sf_cmd_error("Not a valid job number");
		return;
	}
	int pid = job_id_to_pid[job_num];
	if (killpg(pid, SIGSTOP) == -1) {
		sf_cmd_error("Job could not be paused");
		return;
	}
	sf_cmd_ok();
}



void resume_job(char *command) {
	int job_num;
	int expected_args = 1;
	char *args[expected_args];
	if (!process_arguments(command, args, expected_args, expected_args)) {
		sf_cmd_error("Incorrect number of args");
		return;
	}
	if (sscanf(args[0], "%d", &job_num) == -1 || job_id_to_pid[job_num] == 0) {
		sf_cmd_error("Not a valid job number");
		return;
	}
	int pid = job_id_to_pid[job_num];
	if (killpg(pid, SIGCONT) == -1) {
		sf_cmd_error("Job could not be continued");
		return;
	}
	sf_cmd_ok();
}






void change_printer_status(char *command, PRINTER_STATUS status) {
	int expected_args = 1;
	char *args[expected_args];
	if (!process_arguments(command, args, expected_args, expected_args)) {
		sf_cmd_error("Incorrect number of args");
		return;
	}
	PRINTER *printer = find_printer(args[0]);
	if (printer == NULL) {
		sf_cmd_error("Could not find printer");
		return;
	}
	if (printer->status != status) {
		printer->status = status;
		sf_printer_status(args[0], status);
	}
	sf_cmd_ok();
}


PRINTER *find_printer(char *name) {
	for (int i = 0; i < MAX_PRINTERS; i++) {
		if (printers[i] != NULL && strcmp(printers[i]->name, name) == 0) {
			return printers[i];
		}
	}
	return NULL;
}







void run_available_jobs() {
	JOB *job;
	PRINTER *printer;
	for (int i = 0; i < MAX_JOBS; i++) {
		job = jobs[i];
		if (job != NULL && job->status == JOB_CREATED) {
			printer = find_printer_for_job(job);
			if (printer != NULL) {
				job->selected_printer = printer;
				run_job(job, printer);
			}
		}
	}
}

PRINTER *find_printer_for_job(JOB *job) {
	char *to_file, *from_file;
	PRINTER *printer;
	CONVERSION **conversion_path;
	from_file = job->type->name;
	int eligible_bitmap = job->eligible;
	for (int i = 0; i < MAX_PRINTERS; i++) {
		if ((eligible_bitmap & (1 << i)) == 0) continue;
		printer = printers[i];
		if (printer->status == PRINTER_IDLE) {
			to_file = printer->type->name;
			conversion_path = find_conversion_path(from_file, to_file);
			if (conversion_path != NULL) {
				job->conversion_path = conversion_path;
				return printer;
			}
		}
	}
	return NULL;
}


void run_job(JOB *job, PRINTER *printer) {
	int pid;
	CONVERSION **conversion_path = job->conversion_path;
	int printer_descriptor = imp_connect_to_printer(printer->name, printer->type->name, PRINTER_NORMAL);
	if (printer_descriptor == -1) {
		debug("Could not connect to printer.");
		return;
	}
	if ((pid = fork()) == 0) {
		setpgid(0, 0);
		if (!unblock_sigterm_sigpipe()) {
			exit(-1);
		}
		if (conversion_path[0] == NULL) {
			print_no_conversion(job->file, printer_descriptor);
		} else {
			run_conversion_pipeline(job->file, printer_descriptor, conversion_path);
		}
		close(printer_descriptor);
		int exit_status = reap_children();
		free_memory();
		conversions_fini();
		exit(exit_status);
	}
	setpgid(pid, pid);
	close(printer_descriptor);
	update_running_job_statuses(job, printer, conversion_path, pid);
	job_id_to_pid[job->id] = pid;
}

int unblock_sigterm_sigpipe() {
	sigset_t old_mask, sigterm_mask;
	sigemptyset(&sigterm_mask);
	sigaddset(&sigterm_mask, SIGTERM);
	sigaddset(&sigterm_mask, SIGPIPE);
	if (sigprocmask(0, NULL, &old_mask) == -1 || sigprocmask(SIG_UNBLOCK, &sigterm_mask, &old_mask) == -1) {
		return 0;
	}
	return 1;
}

void print_no_conversion(char *filename, int printer_descriptor) {
	if (fork() == 0) {
		int input = open(filename, O_RDONLY);
		debug("file desc: %d", input);
		if (input == -1) exit(1);
		dup2(input, 0);
		dup2(printer_descriptor, 1);
		close(input);
		close(printer_descriptor);
		char *args[] = {"/bin/cat", NULL};
		execvp(args[0], args);
		exit(1);
	}
}

void run_conversion_pipeline(char *filename, int printer_descriptor, CONVERSION **conversion_path) {
	char **cmd_and_args;
	int input, output, fds[2];
	CONVERSION *conversion;
	int index = 0;
	int num_links = count_links_in_conversion_path(conversion_path);
	while ((conversion = conversion_path[index]) != NULL) {
		pipe(fds);
		output = fds[1];
		if (fork() == 0) {
			if (index == 0) input = open(filename, O_RDONLY);
			if (input == -1) exit(1);
			if (index == (num_links - 1)) close(fds[1]), output = printer_descriptor;
			dup2(input, 0);
			dup2(output, 1);
			close(fds[0]);
			close(input);
			close(output);
			if (index != (num_links - 1)) close(printer_descriptor);
			cmd_and_args = conversion->cmd_and_args;
			execvp(cmd_and_args[0], cmd_and_args);
			exit(1);
		}
		if (index != 0) close(input);
		close(output);
		input = fds[0];
		index++;
	}
	close(input);

}

int count_links_in_conversion_path(CONVERSION **path) {
	int count = 0;
	while (path[count] != NULL) {
		count++;
	}
	return count;
}

void update_running_job_statuses(JOB *job, PRINTER *printer, CONVERSION **pipeline, int pid) {
	int links = count_links_in_conversion_path(pipeline);
	char *command_names[links + 1];
	get_command_names(pipeline, command_names);
	sf_job_started(job->id, printer->name, pid, command_names);
	job->status = JOB_RUNNING;
	sf_job_status(job->id, JOB_RUNNING);
	printer->status = PRINTER_BUSY;
	sf_printer_status(printer->name, PRINTER_BUSY);
}

void get_command_names(CONVERSION **pipeline, char **command_names) {
	CONVERSION *conversion;
	int index = 0;
	while ((conversion = pipeline[index]) != NULL) {
		command_names[index] = conversion->cmd_and_args[0];
		index++;
	}
	command_names[index] = NULL;
}

int reap_children() {
	int status, pid;
	int error = 0;
	while ((pid = wait(&status)) > 0) {
		debug("Child %d exiting with status %d\n", pid, status);
		if (WIFSIGNALED(status)) {
			error = WTERMSIG(status);
		} else if (WIFEXITED(status) && (WEXITSTATUS(status) != 0)) {
			error = WEXITSTATUS(status);
		}
	}
	return error;
}