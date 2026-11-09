package main

import (
	"fmt"
	"time"
)

const N = 256

func benchMatrix() int64 {
	A := make([]int64, N*N)
	B := make([]int64, N*N)
	C := make([]int64, N*N)

	for i := 0; i < N; i++ {
		for j := 0; j < N; j++ {
			A[i*N+j] = int64((i + j) % 100)
			B[i*N+j] = int64((i*j + 1) % 100)
			C[i*N+j] = 0
		}
	}

	for i := 0; i < N; i++ {
		for k := 0; k < N; k++ {
			r := A[i*N+k]
			for j := 0; j < N; j++ {
				C[i*N+j] += r * B[k*N+j]
			}
		}
	}
	var total int64 = 0
	for idx := 0; idx < N*N; idx++ {
		total += C[idx]
	}
	return total
}

func main() {
	start := time.Now()
	res := benchMatrix()
	elapsed := time.Since(start).Seconds()
	fmt.Printf("Golang\tmatrix\t%d\t%.6f\n", res, elapsed)
}
