
#include "../thread/thread.h"
#include "../include/syscall_nr.h"
#include "../include/asm/stub.h"
#include "../initer/io/io.h"
#include "../include/asmFunc.h"
#include "../userprog/process.h"
#include "../userprog/wait_exit.h"
#include "../lib/str/str.h"
#include "../include/assert.h"


static const uint8_t g_sig_default[NSIG] = {
    [SIGHUP]   = SIG_ACT_TERM,
    [SIGINT]   = SIG_ACT_TERM,
    [SIGQUIT]  = SIG_ACT_TERM,
    [SIGILL]   = SIG_ACT_TERM,
    [SIGTRAP]  = SIG_ACT_TERM,
    [SIGABRT]  = SIG_ACT_TERM,
    [SIGBUS]   = SIG_ACT_TERM,
    [SIGFPE]   = SIG_ACT_TERM,
    [SIGKILL]  = SIG_ACT_TERM,   
    [SIGUSR1]  = SIG_ACT_TERM,
    [SIGSEGV]  = SIG_ACT_TERM,
    [SIGUSR2]  = SIG_ACT_TERM,
    [SIGPIPE]  = SIG_ACT_TERM,
    [SIGALRM]  = SIG_ACT_TERM,
    [SIGTERM]  = SIG_ACT_TERM,
    [SIGCHLD]  = SIG_ACT_IGN,
    [SIGCONT]  = SIG_ACT_CONT,
    [SIGSTOP]  = SIG_ACT_STOP,   
    [SIGTSTP]  = SIG_ACT_STOP,
    [SIGTTIN]  = SIG_ACT_STOP,
    [SIGTTOU]  = SIG_ACT_STOP,
    [SIGSYS]   = SIG_ACT_TERM,
};

void init_signal_state(struct task_struct* t) {
    t->signal_pending = 0;
    t->signal_mask = 0;
    for (int i = 0; i < NSIG; i++) {
        t->sigactions[i].sa_handler = SIG_DFL;
        t->sigactions[i].sa_mask = 0;
        t->sigactions[i].sa_flags = 0;
        t->sigactions[i].sa_restorer = NULL;
    }
}


void signal_reset_user(struct task_struct* t) {
    init_signal_state(t);
}

int exception_to_signal(int int_no) {
    switch (int_no) {
    case 0:  return SIGFPE;   
    case 6:  return SIGILL;   
    case 13: return SIGSEGV;  
    case 14: return SIGSEGV;  
    case 16: return SIGFPE;   
    case 19: return SIGFPE;   
    default: return 0;        
    }
}

void signal_terminate(struct task_struct* t, int sig) {
    proc_exit(t, 128 + sig);
    
}


static void signal_stop_current(void) {
    struct task_struct* cur = current_task;
    cur->status = TASK_STOPPED;
    if (elem_find(&g_ready_list, &cur->general_tag)) {
        list_remove(&cur->general_tag);
    }
    schedule();
    
}


static void deliver_signal(struct task_struct* cur, struct Registers* r,
                           int sig, struct sigaction* sa) {
    struct sigframe frame;
    frame.restorer = (uint32_t)sa->sa_restorer;
    frame.signo    = (uint32_t)sig;
    frame.eip      = r->eip;
    frame.cs       = r->cs;
    frame.eflags   = r->eflags & ~(1u << 8);   
    frame.user_esp = r->user_esp;
    frame.ss       = r->ss;
    frame.eax = r->eax; frame.ebx = r->ebx; frame.ecx = r->ecx; frame.edx = r->edx;
    frame.esi = r->esi; frame.edi = r->edi; frame.ebp = r->ebp;
    frame.old_mask = cur->signal_mask;

    uint32_t frame_size = sizeof(struct sigframe);
    uint32_t new_esp = (r->user_esp - frame_size) & ~3u;

    
    if (new_esp < USER_STACK3_VADDR) {
        signal_terminate(cur, sig);
    }

    
    memcpy((void*)new_esp, &frame, frame_size);

    if (!(sa->sa_flags & SA_NODEFER)) {
        cur->signal_mask |= (1u << sig);
    }
    cur->signal_mask |= sa->sa_mask;
    
    cur->signal_mask &= ~((1u << SIGKILL) | (1u << SIGSTOP));

    r->user_esp = new_esp;
    r->eip = (uint32_t)sa->sa_handler;
    r->eax = (uint32_t)sig;   
}

void check_pending_signals(struct Registers* r) {
    struct task_struct* cur = current_task;
    if (cur == NULL) {
        return;
    }
    
    if ((r->cs & 3) != 3) {
        return;
    }
    if (cur->signal_pending == 0) {
        return;
    }

    for (;;) {
        uint32_t deliverable = cur->signal_pending & ~cur->signal_mask;
        if (deliverable == 0) {
            break;
        }
        int sig = __builtin_ctz(deliverable);
        if (sig >= NSIG) {
            cur->signal_pending = 0;
            break;
        }
        cur->signal_pending &= ~(1u << sig);

        struct sigaction* sa = &cur->sigactions[sig];
        void (*handler)(int) = sa->sa_handler;

        
        if (sig == SIGKILL) {
            signal_terminate(cur, sig);
        }
        
        if (sig == SIGSTOP) {
            signal_stop_current();
            continue;
        }

        if (handler == SIG_DFL) {
            uint8_t act = g_sig_default[sig];
            if (act == SIG_ACT_IGN) {
                continue;                          
            } else if (act == SIG_ACT_STOP) {
                signal_stop_current();             
                continue;
            } else if (act == SIG_ACT_CONT) {
                continue;                          
            } else {
                signal_terminate(cur, sig);        
            }
        } else if (handler == SIG_IGN) {
            continue;                              
        }

        
        deliver_signal(cur, r, sig, sa);
        return;
    }
}

int sys_sigaction(int sig, const struct sigaction* act, struct sigaction* old) {
    if (sig < 1 || sig >= NSIG) {
        return -1;
    }
    
    if (sig == SIGKILL || sig == SIGSTOP) {
        return -1;
    }
    if (old) {
        memcpy((void*)old, &current_task->sigactions[sig], sizeof(struct sigaction));
    }
    if (act) {
        memcpy(&current_task->sigactions[sig], (const void*)act, sizeof(struct sigaction));
    }
    return 0;
}

int sys_sigprocmask(int how, const sigset_t* set, sigset_t* oldset) {
    if (oldset) {
        *oldset = current_task->signal_mask;
    }
    if (set) {
        sigset_t s = *set;
        if (how == SIG_BLOCK) {
            current_task->signal_mask |= s;
        } else if (how == SIG_UNBLOCK) {
            current_task->signal_mask &= ~s;
        } else if (how == SIG_SETMASK) {
            current_task->signal_mask = s;
        } else {
            return -1;
        }
        
        current_task->signal_mask &= ~((1u << SIGKILL) | (1u << SIGSTOP));
    }
    return 0;
}

int sys_kill(int pid, int sig) {
    if (sig < 0 || sig >= NSIG) {
        return -1;
    }
    if (pid == 0) {
        pid = (int)current_task->pid;
    }
    struct task_struct* t = pid2thread(pid);
    if (t == NULL) {
        return -1;
    }
    if (sig == 0) {
        return 0;   
    }

    
    if (sig == SIGKILL) {
        thread_kill_pid((uint32_t)pid);
        return 0;
    }

    
    if (sig == SIGCONT && t->status == TASK_STOPPED) {
        t->status = TASK_READY;
        if (!elem_find(&g_ready_list, &t->general_tag)) {
            list_push(&g_ready_list, &t->general_tag);
        }
        t->signal_pending &= ~(1u << SIGCONT);
        return 0;
    }

    t->signal_pending |= (1u << sig);
    return 0;
}

void sys_sigreturn(struct Registers* r) {
    struct task_struct* cur = current_task;
    
    struct sigframe* sf = (struct sigframe*)(r->user_esp - 4);

    r->eip      = sf->eip;
    r->cs       = sf->cs;
    r->eflags   = sf->eflags;
    r->user_esp = sf->user_esp;
    r->ss       = sf->ss;
    r->eax = sf->eax; r->ebx = sf->ebx; r->ecx = sf->ecx; r->edx = sf->edx;
    r->esi = sf->esi; r->edi = sf->edi; r->ebp = sf->ebp;

    cur->signal_mask = sf->old_mask;
    cur->signal_mask &= ~((1u << SIGKILL) | (1u << SIGSTOP));
}
