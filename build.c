#define _POSIX_C_SOURCE 200809L

#include <sys/wait.h>

#include <stddef.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>

#define DT_REG 8
#define OUTPUT_DIR  "public"
#define PAGES_DIR "./pages"
#define MAX_POSTS   100
#define MAX_LINE    1024
#define MAX_MD      (1024 * 64)

char *html_header =
	"<!DOCTYPE html>\n"
	"<html lang=\"en\">\n"
	"<head>\n"
	"\t<meta charset=\"UTF-8\"/>\n"
	"\t<title>%s</title>\n"
	"\t<link rel=\"stylesheet\" href=\"/css/style.css\">\n"
	"</head>\n"
    "<header><nav><a href='#menu'>Menu</a></nav></header>"
	"<body>\n";

char *html_footer = 
    "<footer role='contentinfo'>"
        "<nav id='menu'>"
            "<ul>"
                "<li><a href='/'>Home</a></li>"
                "<li><a href='/about'>About</a></li>"
                "<li><a href='/posts'>Posts</a></li>"
            "<ul>"
        "</nav>"
    "</footer>"
    "</body>\n"
    "</html>\n";

struct Post {
    char filename[256];
    char title[256];
    char date[64];
    char tags[256];
};

struct Post posts[MAX_POSTS];
int post_count = 0;

void
mkdir_p(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        mkdir(path, 0755);
    }
}

void
die(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

void 
copy_css(void) {
    system("cp -r css public/");
}

void 
parse_post(const char *filepath, struct Post *post, char *md_buf, size_t *md_len) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) die("fopen: %s", filepath);

    const char *fname = filepath + strlen(PAGES_DIR"/posts/");
    size_t name_len = strlen(fname)-3;
    memcpy(post->filename, fname, name_len);

    char line[MAX_LINE];
    int skip = 0;
    size_t md_pos = 0;
    while (fgets(line, sizeof(line), fp)) {
        // printf("%d> %s", line[0] == '\n', line);
        if (strncmp(line, "title:", 6) == 0) {
            sscanf(line + 6, " %[^\n]", post->title);
            continue;
        } else if (strncmp(line, "date:", 5) == 0) {
            sscanf(line + 5, " %[^\n]", post->date);
            continue;
        } else if (strncmp(line, "tags:", 5) == 0) {
            sscanf(line + 5, " %[^\n]", post->tags);
            continue;
        }

        if ((line[0] == '\n') == 1 && skip == 0){
            skip = 1;
            continue;
        }

        if (skip) {
            size_t line_len = strlen(line);
            if (md_pos + line_len >= MAX_MD) break;

            memcpy(md_buf + md_pos, line, line_len);
            md_pos += line_len;
            md_buf[md_pos] = '\n';
        }
    }
    md_buf[md_pos] = '\0';
    *md_len = md_pos;

    fclose(fp);
}

void 
run_smu(const char *md_buf, size_t md_len, char *out_buf, size_t *out_len) {
    int in_pipe[2], out_pipe[2];
    pid_t pid;
    ssize_t n;
    size_t total = 0;

    pipe(in_pipe);
    pipe(out_pipe);

    pid = fork();
    if (pid == 0) {
        /* Child */
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[1]);
        close(out_pipe[0]);
        execlp("smu", "smu", NULL);
        _exit(1);
    }

    /* Parent */
    close(in_pipe[0]);
    close(out_pipe[1]);

    /* Write markdown */
    write(in_pipe[1], md_buf, md_len);
    close(in_pipe[1]);

    /* Read HTML */
    while ((n = read(out_pipe[0], out_buf + total, MAX_MD - total - 1)) > 0) {
        total += n;
    }
    out_buf[total] = '\0';
    *out_len = total;

    close(out_pipe[0]);
    waitpid(pid, NULL, 0);
}

void
build_post(const struct Post *post, const char *md_buf, const size_t md_len){
    char html_buf[MAX_MD];
    size_t html_len;
    char outpath[512];
    FILE *out;

    snprintf(outpath, sizeof(outpath), OUTPUT_DIR "/posts/%s.html", post->filename);

    run_smu(md_buf, md_len, html_buf, &html_len);

    out = fopen(outpath, "w");
    if (!out) die("fopen: %s", outpath);

    fprintf(out, html_header, post->title);
    fprintf(out, "<h1>%s</h1><p><strong>Date:</strong> %s <br> <strong>Tags:</strong> %s</p>",
            post->title, post->date, post->tags);

    fwrite(html_buf, 1, html_len, out);

    fprintf(out, html_footer, NULL);
    fclose(out);
}

void
build_index_post(){
    FILE *in = fopen(PAGES_DIR"/posts/index.md", "r");
    if (!in) die("fopen: %s", PAGES_DIR"/posts/index.md");
    
    char md_buf[MAX_MD];
    size_t md_len = fread(md_buf, 1, MAX_MD - 1, in);
    md_buf[md_len] = '\0';

    fclose(in);

    char html_buf[MAX_MD];
    size_t html_len;
    run_smu(md_buf, md_len, html_buf, &html_len);

    FILE *out = fopen(OUTPUT_DIR "/posts/index.html", "w");
    if (!out) die("fopen: %s", OUTPUT_DIR "/posts/index.html");

    fprintf(out, html_header, "All Post");
    fwrite(html_buf, 1, html_len, out);

    fprintf(out, "<ul class='posts'>");
    for(int i=0; i <post_count; ++i) {
        fprintf(out, "<li><span>%s</span><a href='/posts/%s.html'>%s</ahref></li>", posts[i].date, posts[i].filename, posts[i].title);
    }
    fprintf(out, "</ul>");

    fprintf(out, html_footer, NULL);
    fclose(out);

}

void 
build_index(void) {
    FILE *in = fopen(PAGES_DIR"/index.md", "r");
    if (!in) die("fopen: %s", PAGES_DIR"/index.md");
    
    char md_buf[MAX_MD];
    size_t md_len = fread(md_buf, 1, MAX_MD - 1, in);
    md_buf[md_len] = '\0';

    fclose(in);

    char html_buf[MAX_MD];
    size_t html_len;
    run_smu(md_buf, md_len, html_buf, &html_len);

    FILE *out = fopen(OUTPUT_DIR "/index.html", "w");
    if (!out) die("fopen: %s", OUTPUT_DIR "/index.html");

    fprintf(out, html_header, "Mahesa Rohman");
    fwrite(html_buf, 1, html_len, out);

    fprintf(out, "<h2>Recent Post</h2>");
    fprintf(out, "<ul class='posts'>");
    for(int i=0; i <post_count; ++i) {
        if (i > 5) break;
        fprintf(out, "<li><span>%s</span><a href='/posts/%s.html'>%s</ahref></li>", posts[i].date, posts[i].filename, posts[i].title);
    }
    fprintf(out, "</ul>");
    fprintf(out, "<p><a href='/posts'>View all posts</href></p>");

    fprintf(out, html_footer, NULL);
    fclose(out);
}

void 
build_about(void) {
    FILE *in = fopen(PAGES_DIR"/about/index.md", "r");
    if (!in) die("fopen: %s", PAGES_DIR"/about/index.md");
    
    char md_buf[MAX_MD];
    size_t md_len = fread(md_buf, 1, MAX_MD - 1, in);
    md_buf[md_len] = '\0';

    fclose(in);

    char html_buf[MAX_MD];
    size_t html_len;
    run_smu(md_buf, md_len, html_buf, &html_len);

    FILE *out = fopen(OUTPUT_DIR "/about/index.html", "w");
    if (!out) die("fopen: %s", OUTPUT_DIR "/about/index.html");;

    fprintf(out, html_header, "Mahesa Rohman");
    fwrite(html_buf, 1, html_len, out);

    fprintf(out, html_footer, NULL);
    fclose(out);
}

int 
compare_posts(const void *a, const void *b) {
    const struct Post *pa = a, *pb = b;
    return strcmp(pb->date, pa->date); /* Descending */
}

int 
main () {
    mkdir(OUTPUT_DIR, 0755);
    mkdir(OUTPUT_DIR"/posts", 0755);   
    mkdir(OUTPUT_DIR"/about", 0755);   
    copy_css();

    DIR *dir = opendir(PAGES_DIR "/posts");
    if (!dir)  die("opendir posts");

    char md_buf[MAX_MD];
    size_t md_len;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
         if (entry->d_type != DT_REG || strcmp(entry->d_name, "index.md") == 0) {
            continue;
        }
        char filepath[512];
        snprintf(filepath, sizeof(filepath), PAGES_DIR"/posts/%s", entry->d_name);

        parse_post(filepath, &posts[post_count], md_buf, &md_len);
        build_post(&posts[post_count], md_buf, md_len);

        post_count++;
    }
    closedir(dir);

    qsort(posts, post_count, sizeof(struct Post), compare_posts);

    build_index_post();

    build_index();

    build_about();

    return 0;
}
