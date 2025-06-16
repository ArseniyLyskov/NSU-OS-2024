
#define _GNU_SOURCE
#define __EXTENSIONS__
#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

struct task_arg {
    int srcFd;
    int dstFd;
    char *name;
    mode_t mode;
    int is_dir;
};

void *dir_thread(void *arg);
void *file_thread(void *arg);

void process_dir(int srcFd, int dstFd) {
    long name_max = fpathconf(srcFd, _PC_NAME_MAX);
    if (name_max < 0) name_max = 255;
    size_t bufsize = offsetof(struct dirent, d_name) + name_max + 1;
    struct dirent *entry = malloc(bufsize);
    struct dirent *result;
    int dirfd_dup = dup(srcFd);
    if (dirfd_dup < 0) {
        perror("dup");
        close(srcFd);
        close(dstFd);
        free(entry);
        return;
    }
    DIR *dirp = fdopendir(dirfd_dup);
    if (!dirp) {
        perror("fdopendir");
        close(dirfd_dup);
        close(srcFd);
        close(dstFd);
        free(entry);
        return;
    }
    pthread_t *threads = NULL;
    int tcount = 0, tcap = 0;
    while (1) {
        int r = readdir_r(dirp, entry, &result);
        if (r != 0 || result == NULL) break;
        char *name = result->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        struct stat st;
        if (fstatat(srcFd, name, &st, AT_SYMLINK_NOFOLLOW) < 0) {
            perror("fstatat");
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            mode_t mode = st.st_mode & 0777;
            while (mkdirat(dstFd, name, mode) < 0) {
                if (errno == EEXIST) break;
                if (errno == EMFILE) { sleep(1); continue; }
                perror("mkdirat");
                break;
            }
            struct task_arg *targ = malloc(sizeof(struct task_arg));
            targ->srcFd = srcFd;
            targ->dstFd = dstFd;
            targ->name = strdup(name);
            targ->mode = mode;
            targ->is_dir = 1;
            pthread_t tid;
            if (pthread_create(&tid, NULL, dir_thread, targ) != 0) {
                perror("pthread_create");
                free(targ->name);
                free(targ);
                continue;
            }
            if (tcount == tcap) {
                tcap = tcap ? tcap * 2 : 4;
                threads = realloc(threads, tcap * sizeof(pthread_t));
            }
            threads[tcount++] = tid;
        } else if (S_ISREG(st.st_mode)) {
            mode_t mode = st.st_mode & 0777;
            struct task_arg *targ = malloc(sizeof(struct task_arg));
            targ->srcFd = srcFd;
            targ->dstFd = dstFd;
            targ->name = strdup(name);
            targ->mode = mode;
            targ->is_dir = 0;
            pthread_t tid;
            if (pthread_create(&tid, NULL, file_thread, targ) != 0) {
                perror("pthread_create");
                free(targ->name);
                free(targ);
                continue;
            }
            if (tcount == tcap) {
                tcap = tcap ? tcap * 2 : 4;
                threads = realloc(threads, tcap * sizeof(pthread_t));
            }
            threads[tcount++] = tid;
        }
    }
    closedir(dirp);
    free(entry);
    for (int i = 0; i < tcount; i++) {
        pthread_join(threads[i], NULL);
    }
    free(threads);
    close(srcFd);
    close(dstFd);
}

void *dir_thread(void *arg) {
    struct task_arg *targ = arg;
    int parentSrcFd = targ->srcFd;
    int parentDstFd = targ->dstFd;
    char *name = targ->name;
    free(targ);
    int newSrcFd, newDstFd;
    while ((newSrcFd = openat(parentSrcFd, name, O_RDONLY)) < 0 && errno == EMFILE) {
        sleep(1);
    }
    if (newSrcFd < 0) {
        perror("openat src dir");
        free(name);
        return NULL;
    }
    while ((newDstFd = openat(parentDstFd, name, O_RDONLY)) < 0 && errno == EMFILE) {
        sleep(1);
    }
    if (newDstFd < 0) {
        perror("openat dst dir");
        close(newSrcFd);
        free(name);
        return NULL;
    }
    process_dir(newSrcFd, newDstFd);
    free(name);
    return NULL;
}

void *file_thread(void *arg) {
    struct task_arg *targ = arg;
    int parentSrcFd = targ->srcFd;
    int parentDstFd = targ->dstFd;
    char *name = targ->name;
    mode_t mode = targ->mode;
    free(targ);
    int srcFile, dstFile;
    while ((srcFile = openat(parentSrcFd, name, O_RDONLY)) < 0 && errno == EMFILE) {
        sleep(1);
    }
    if (srcFile < 0) {
        perror("openat src file");
        free(name);
        return NULL;
    }
    while ((dstFile = openat(parentDstFd, name, O_WRONLY | O_CREAT | O_TRUNC, mode)) < 0 && errno == EMFILE) {
        sleep(1);
    }
    if (dstFile < 0) {
        perror("openat dst file");
        close(srcFile);
        free(name);
        return NULL;
    }
    char buf[8192];
    ssize_t n;
    while ((n = read(srcFile, buf, sizeof(buf))) > 0) {
        char *p = buf;
        while (n > 0) {
            ssize_t w = write(dstFile, p, n);
            if (w <= 0) {
                perror("write");
                close(srcFile);
                close(dstFile);
                free(name);
                return NULL;
            }
            n -= w;
            p += w;
        }
    }
    if (n < 0) perror("read");
    close(srcFile);
    close(dstFile);
    free(name);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <source_dir> <destination_dir>\n", argv[0]);
        return 1;
    }

    const char *srcPath = argv[1];
    const char *dstPath = argv[2];

    int srcRoot = open(srcPath, O_RDONLY);
    if (srcRoot < 0) {
        perror("open source");
        return 1;
    }

    struct stat st;
    if (stat(dstPath, &st) < 0) {
        if (mkdir(dstPath, 0777) < 0) {
            perror("mkdir");
            close(srcRoot);
            return 1;
        }
    }

    int dstRoot = open(dstPath, O_RDONLY);
    if (dstRoot < 0) {
        perror("open dest");
        close(srcRoot);
        return 1;
    }

    process_dir(srcRoot, dstRoot);

    puts("OK");
    return 0;
}
