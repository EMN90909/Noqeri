package main

import (
	"fmt"
	"time"
)

const N = 2000

func benchSort() int64 {
	arr := make([]int64, N)
	var seed uint64 = 12345
	for i := 0; i < N; i++ {
		seed = (seed*1103515245 + 12345) % 2147483648
		arr[i] = int64(seed % 100000)
	}
	for j := 1; j < N; j++ {
		key := arr[j]
		k := j - 1
		for k >= 0 && arr[k] > key {
			arr[k+1] = arr[k]
			k--
		}
		arr[k+1] = key
	}
	return arr[N-1]
}

func main() {
	start := time.Now()
	res := benchSort()
	elapsed := time.Since(start).Seconds()
	fmt.Printf("Golang\tsorting\t%d\t%.6f\n", res, elapsed)
}
