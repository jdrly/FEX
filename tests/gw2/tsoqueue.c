/* gw2 (jdrly/gw2-on-mac#113): whether translated code keeps x86 memory ordering across syscalls,
 * for FEX_HARDWARETSOSYSCALLS=keep, with switch as the control. A Win64 x86_64 program, built
 * with llvm-mingw.
 *
 *   tsoqueue.exe [<seconds>]
 *
 * Four threads in a ring for <seconds> (60 by default). Each thread produces into a queue its
 * successor consumes, and consumes the queue its predecessor produces into. A queue is a ring of
 * 64 slots with one producer and one consumer, and it has no fences: an item's four words and the
 * index that publishes it are plain stores, and the consumer's reads are plain loads, kept in
 * order by the compiler only. x86's ordering makes that correct. Without it, a consumer can see
 * the index before the item (store-store or load-load reordering), or a producer can overwrite a
 * slot the consumer is still reading (load-store reordering). Every item carries its sequence
 * number and three words derived from it, so either shows as a wrong item.
 *
 * Every pass of a thread's loop makes a syscall (QueryPerformanceCounter, which also reads the
 * deadline), and a thread that made no progress 64 passes in a row yields (SwitchToThread). So the
 * plain loads and stores run between thousands of returns from syscalls a second, which is where
 * FEX_HARDWARETSOSYSCALLS=keep changes what happens.
 *
 * At the deadline each thread stops producing, and consumes until its predecessor has stopped and
 * its queue is empty. It passes when every item arrived once, in order, and intact:
 *
 *   gw2 test tsoqueue: <n> items, <syscalls> syscalls
 *   gw2 test tsoqueue: passed */
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#define THREADS 4
#define SLOTS 64
#define BARRIER() __asm__ __volatile__("" ::: "memory")

struct item
{
    unsigned long long seq, a, b, c;
};

struct queue
{
    volatile unsigned long long head __attribute__((aligned(64))); /* written by the producer */
    volatile unsigned long long tail __attribute__((aligned(64))); /* written by the consumer */
    volatile struct item slots[SLOTS] __attribute__((aligned(64)));
    volatile LONG stopped __attribute__((aligned(64)));            /* the producer's last item is published */
};

struct worker
{
    int index;
    unsigned long long sent, received, bad, syscalls;
};

static struct queue queues[THREADS];
static struct worker workers[THREADS];
static LARGE_INTEGER deadline;
static volatile LONG reports;

static unsigned long long word_a(unsigned long long s) { return s * 0x9e3779b97f4a7c15ull; }
static unsigned long long word_b(unsigned long long s) { return ~s; }
static unsigned long long word_c(unsigned long long s) { return s ^ 0x5555aaaa5555aaaaull; }

static DWORD WINAPI run(void *arg)
{
    struct worker *w = arg;
    struct queue *out = &queues[w->index];
    struct queue *in = &queues[(w->index + THREADS - 1) % THREADS];
    unsigned long long out_tail = 0, idle = 0;
    int producing = 1;

    for (;;)
    {
        int progress = 0;
        LARGE_INTEGER now;

        QueryPerformanceCounter(&now);
        w->syscalls++;
        if (producing && now.QuadPart >= deadline.QuadPart)
        {
            producing = 0;
            InterlockedExchange(&out->stopped, 1);
        }

        if (producing)
        {
            unsigned long long h = out->head;
            if (h - out_tail == SLOTS)
                out_tail = out->tail;
            if (h - out_tail < SLOTS)
            {
                volatile struct item *s = &out->slots[h % SLOTS];
                s->seq = h;
                s->a = word_a(h);
                s->b = word_b(h);
                s->c = word_c(h);
                BARRIER();
                out->head = h + 1;
                w->sent++;
                progress = 1;
            }
        }

        {
            int stopped = in->stopped;
            unsigned long long t = in->tail, h;
            BARRIER();
            h = in->head;
            if (t != h)
            {
                volatile struct item *s = &in->slots[t % SLOTS];
                unsigned long long seq = s->seq, a = s->a, b = s->b, c = s->c;
                BARRIER();
                in->tail = t + 1;
                if (seq != t || a != word_a(t) || b != word_b(t) || c != word_c(t))
                {
                    if (InterlockedIncrement(&reports) <= 10)
                        printf("gw2 test tsoqueue: thread %d slot %llu: seq %llu a %llx b %llx c %llx\n", w->index, t, seq, a, b, c);
                    w->bad++;
                }
                w->received++;
                progress = 1;
            }
            else if (stopped && !producing)
                break;
        }

        if (progress)
            idle = 0;
        else if (++idle >= 64)
        {
            SwitchToThread();
            w->syscalls++;
            idle = 0;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    HANDLE threads[THREADS];
    LARGE_INTEGER freq, now;
    unsigned long long seconds = 60, items = 0, syscalls = 0;
    int failed = 0;

    if (argc > 2 || (argc == 2 && !(seconds = strtoull(argv[1], NULL, 10))))
    {
        printf("gw2 test tsoqueue: usage: tsoqueue.exe [<seconds>]\n");
        return 2;
    }
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    deadline.QuadPart = now.QuadPart + (LONGLONG)seconds * freq.QuadPart;

    for (int i = 0; i < THREADS; i++)
    {
        workers[i].index = i;
        if (!(threads[i] = CreateThread(NULL, 0, run, &workers[i], 0, NULL)))
        {
            printf("gw2 test tsoqueue: CreateThread failed (%lu)\n", GetLastError());
            return 1;
        }
    }
    WaitForMultipleObjects(THREADS, threads, TRUE, INFINITE);

    for (int i = 0; i < THREADS; i++)
    {
        const struct worker *producer = &workers[(i + THREADS - 1) % THREADS];
        if (workers[i].received != producer->sent || workers[i].bad)
        {
            printf("gw2 test tsoqueue: thread %d got %llu of %llu items, %llu wrong\n", i, workers[i].received,
                   producer->sent, workers[i].bad);
            failed = 1;
        }
        items += workers[i].received;
        syscalls += workers[i].syscalls;
    }
    printf("gw2 test tsoqueue: %llu items, %llu syscalls\n", items, syscalls);
    if (failed || !items)
        return 1;
    printf("gw2 test tsoqueue: passed\n");
    return 0;
}
