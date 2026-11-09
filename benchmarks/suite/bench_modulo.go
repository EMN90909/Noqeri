package main

import (
	"fmt"
	"time"
)

func benchModulo(iterations int64) int64 {
	var sum int64 = 0
	for i := int64(1); i <= iterations; i++ {
		m := (i*37 + 13) % 997
		sum += m
	}
	return sum
}

func main() {
	start := time.Now()
	res := benchModulo(10000000)
	elapsed := time.Since(start).Seconds()
	fmt.Printf("Golang\tmodulo\t%d\t%.6f\n", res, elapsed)
}
