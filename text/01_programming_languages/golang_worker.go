package main

import (
	"context"
	"fmt"
	"sync"
	"time"
)

type Job struct {
	ID    int
	Task  string
	Delay time.Duration
}

type Result struct {
	JobID  int
	Output string
	Err    error
}

func Worker(ctx context.Context, id int, jobs <-chan Job, results chan<- Result, wg *sync.WaitGroup) {
	defer wg.Done()
	for {
		select {
		case <-ctx.Done():
			return
		case job, ok := <-jobs:
			if !ok {
				return
			}
			time.Sleep(job.Delay)
			results <- Result{
				JobID:  job.ID,
				Output: fmt.Sprintf("Worker %d completed task [%s]", id, job.Task),
			}
		}
	}
}

func main() {
	jobs := make(chan Job, 10)
	results := make(chan Result, 10)
	var wg sync.WaitGroup

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	for w := 1; w <= 3; w++ {
		wg.Add(1)
		go Worker(ctx, w, jobs, results, &wg)
	}

	for j := 1; j <= 5; j++ {
		jobs <- Job{ID: j, Task: fmt.Sprintf("Preview-Render-%d", j), Delay: 50 * time.Millisecond}
	}
	close(jobs)

	wg.Wait()
	close(results)

	for res := range results {
		fmt.Println(res.Output)
	}
}
