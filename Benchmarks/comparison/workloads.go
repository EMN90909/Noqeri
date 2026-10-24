package main

import (
  "fmt"
  "os"
)

func modulo() {
    var acc uint64
    for i := uint64(0); i < 5000000; i++ {
        acc = (acc+((i*17+13)%97))%1000000007
    }
    fmt.Println(acc)
}
func matrix() {
    var a,b,c [256]int64
    for i:=uint64(0);i<256;i++ {
        a[i]=int64((i*17+3)%97)
        b[i]=int64((i*29+11)%89)
    }
    for pass:=0;pass<200;pass++ {
        for row:=0;row<16;row++ {
            for col:=0;col<16;col++ {
                var sum int64
                for k:=0;k<16;k++ {
                    sum+=a[row*16+k]*b[k*16+col]
                }
                c[row*16+col]=sum
            }
        }
    }
    fmt.Println(c[0])
    fmt.Println(c[255])
}
func reshuffled() {
    var values [256]int64
    var checksum uint64
    for round:=uint64(0);round<200;round++ {
        for i:=uint64(0);i<256;i++ {
            values[i]=int64((i*7919+(round+1)*104729)%65521)
        }
        for a:=1;a<256;a++ {
            key:=values[a]
            b:=a
            for b>0&&values[b-1]>key {
                values[b]=values[b-1]
                b--
            }
            values[b]=key
        }
        checksum+=uint64(values[0]+values[255])
    }
    fmt.Println(checksum)
}
func main() {
    if len(os.Args)!=2 { os.Exit(2) }
    switch os.Args[1] {
    case "modulo":modulo()
    case "matrix":matrix()
    case "sorting_reshuffled":reshuffled()
    default:os.Exit(2)
    }
}
