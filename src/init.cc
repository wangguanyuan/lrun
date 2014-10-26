////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2012-2014 Jun Wu <quark@zju.edu.cn>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
////////////////////////////////////////////////////////////////////////////////


// Dummy init process running in a pid namespace
// Provides a pid translate service (via unix socket)

#include "init.h"
#include "common.h"

#include <netinet/in.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <signal.h>
#include <string>


using std::string;
namespace ni = lrun::init;

static void init_signal_handler(int signal) {
    if (signal == SIGCHLD) {
        int status;
        while (waitpid(-1, &status, WNOHANG) > 0);
    } else {
        exit(1);
    }
}

static void process_pid_translate_request(int cfd) {
    long pid;
    if (recv(cfd, &pid, sizeof pid, MSG_WAITALL) == -1) return;
    INFO("got pid: %ld\n", pid);

    struct msghdr msgh;
    struct iovec iov;
    ssize_t ns;

    msgh.msg_name = NULL;
    msgh.msg_namelen = 0;
    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;
    iov.iov_base = &pid;  // pid as dummy data
    iov.iov_len = sizeof(pid);

    struct ucred *ucp;
    union {
        struct cmsghdr cmh;
        char   control[CMSG_SPACE(sizeof(struct ucred))];
    } control_un;

    msgh.msg_control = control_un.control;
    msgh.msg_controllen = sizeof(control_un.control);

    control_un.cmh.cmsg_len = CMSG_LEN(sizeof(struct ucred));
    control_un.cmh.cmsg_level = SOL_SOCKET;
    control_un.cmh.cmsg_type = SCM_CREDENTIALS;

    ucp = (struct ucred *) CMSG_DATA(&control_un.cmh);
    ucp->pid = pid;
    ucp->uid = 0;
    ucp->gid = 0;

    ns = sendmsg(cfd, &msgh, 0);
    if (ns == -1) ERROR("sendmsg");
}

void ni::start_pid_translate_server(const string& socket_path) {
    int sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    INFO("socket: %s", socket_path.c_str());
    if (sfd == -1) {
        ERROR("socket");
        return;
    }

    struct sockaddr_un sun, cun;
    sun.sun_family = AF_UNIX;
    remove(socket_path.c_str());
    strncpy(sun.sun_path, socket_path.c_str(), sizeof sun.sun_path);

    if (bind(sfd, (struct sockaddr*)&sun, sizeof(sun)) == -1) {
        ERROR("bind");
        return;
    }

    chmod(socket_path.c_str(), 0777);

    int backlog = SOMAXCONN;
    if (listen (sfd, backlog) == -1) {
        perror ("listen");
        return;
    }

    int cfd;
    socklen_t cun_size = sizeof cun;

    while ((cfd = accept(sfd, (struct sockaddr*)&cun, &cun_size)) != -1) {
        process_pid_translate_request(cfd);
    }
}

void ni::register_signal_handlers() {
    struct sigaction action;
    action.sa_handler = init_signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGKILL, &action, NULL);
    sigaction(SIGHUP, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGABRT, &action, NULL);
    sigaction(SIGQUIT, &action, NULL);
    sigaction(SIGPIPE, &action, NULL);
    sigaction(SIGALRM, &action, NULL);
    sigaction(SIGCHLD, &action, NULL);
}
