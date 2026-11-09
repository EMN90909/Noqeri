package main

import (
	"fmt"
	"time"
)

func main() {
	start := time.Now()
	var checksum int64 = 0
	for y := int64(0); y < 512; y++ {
		for x := int64(0); x < 512; x++ {
			r := (x + y * 3) % 256
			g := (x * 7 + y * 13) % 256
			b := (x + y) % 256
			pixel := (r * 65536) + (g * 256) + b
			checksum = checksum + pixel
		}
	}
	elapsed := time.Since(start).Seconds()
	fmt.Printf("Go\timage\t%d\t%.6f\n", checksum, elapsed)
}
