#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

// --- Colors ---
#define PURPLE "\033[0;35m"
#define GRAY   "\033[0;37m"
#define GREEN  "\033[1;32m"
#define RED    "\033[1;31m"
#define YELLOW "\033[1;33m"
#define NC     "\033[0m"

volatile sig_atomic_t timeout_flag = 0;

void handle_alarm(int sig) {
    (void)sig;
    timeout_flag = 1;
}

// Run a command with a timeout and measure CPU usage
int run_cmd(char *cmd, char *arg1, char *arg2,
            double *user, double *sys, int timeout_sec) {
    struct rusage usage_before, usage_after;
    pid_t pid;
    int status;

    timeout_flag = 0;

    getrusage(RUSAGE_CHILDREN, &usage_before);

    pid = fork();
    if (pid == 0) {
        execlp(cmd, cmd, arg1, arg2, (char *)NULL);
        perror("exec failed");
        exit(1);
    }

    // install signal handler for SIGALRM
    struct sigaction sa;
    sa.sa_handler = handle_alarm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // important: do NOT set SA_RESTART
    sigaction(SIGALRM, &sa, NULL);

    alarm(timeout_sec);

    pid_t w = waitpid(pid, &status, 0);

    if (timeout_flag) {
        // timeout hit → kill child
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return 124;
    }

    alarm(0);  // cancel alarm if finished early

    if (w == -1) {
        perror("waitpid failed");
        return 1;
    }

    getrusage(RUSAGE_CHILDREN, &usage_after);

    // compute user/sys time diff
    *user = (usage_after.ru_utime.tv_sec - usage_before.ru_utime.tv_sec) +
            (usage_after.ru_utime.tv_usec - usage_before.ru_utime.tv_usec) / 1e6;
    *sys  = (usage_after.ru_stime.tv_sec - usage_before.ru_stime.tv_sec) +
            (usage_after.ru_stime.tv_usec - usage_before.ru_stime.tv_usec) / 1e6;

    return WEXITSTATUS(status);
}

int main() {
    char *dirs[] = {"testdir"};
    char *regex = "*.txt";
    int num_dirs = sizeof(dirs) / sizeof(dirs[0]);

    for (int i = 0; i < num_dirs; i++) {
        printf(PURPLE "BENCH %d " NC YELLOW "(dir=%s, regex=%s)\n" NC,
               i+1, dirs[i], regex);

        double user1 = 0, sys1 = 0, user2 = 0, sys2 = 0;
        double total1 = 0, total2 = 0;
        int rc1 = run_cmd("find", dirs[i], regex, &user1, &sys1, 1);
        int rc2 = run_cmd("./stupid", "", "", &user2, &sys2, 1);

        if (rc1 == 124) {
            printf("find:\t" GRAY "[STALL]\n" NC);
        } else {
            total1 = user1 + sys1;
            printf("find:\t" GRAY "[%.3f]\n" NC, total1);
        }

        if (rc2 == 124) {
            printf("ffind:\t" GREEN "[STALL]\n" NC);
        } else {
            total2 = user2 + sys2;
            printf("ffind:\t" GREEN "[%.3f]\n" NC, total2);
        }

        if (rc1 == 124 || rc2 == 124) {
            printf("diff:\t" RED "[STALL]\n" NC);
        } else {
            double diff = total1 - total2;
            if (diff > 0.0)
                printf("diff:\t" RED "[%.3f]\n" NC, diff);
            else if (diff < 0.0)
                printf("diff:\t" GREEN "[%.3f]\n" NC, diff);
            else
                printf("diff:\t" GRAY "[0.000]\n" NC);
        }
        printf("\n");
    }
    return 0;
}
