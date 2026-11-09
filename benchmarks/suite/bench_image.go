package main

import (
	"fmt"
	"time"
)

func main() {
	start := time.Now()
	pixels := make([]int64, 512*512)
	for y := int64(0); y < 512; y++ {
		for x := int64(0); x < 512; x++ {
			r := (x + y*3) % 256
			g := (x*7 + y*13) % 256
			b := (x + y) % 256
			pixel := (r * 65536) + (g * 256) + b
			pixels[y*512+x] = pixel
		}
	}
	var checksum int64 = 0
	for i := 0; i < 512*512; i++ {
		checksum += pixels[i]
	}
	elapsed := time.Since(start).Seconds()
	fmt.Printf("Golang\timage\t%d\t%.6f\n", checksum, elapsed)
}
