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
	var checksum int64 = 0
	for i := 0; i < N; i++ {
		if i < N-1 && arr[i] > arr[i+1] {
			panic("Sorting verification failed")
		}
		checksum += arr[i] * int64(i+1)
	}
	return checksum
}

func main() {
	start := time.Now()
	res := benchSort()
	elapsed := time.Since(start).Seconds()
	fmt.Printf("Golang\tsorting\t%d\t%.6f\n", res, elapsed)
}
