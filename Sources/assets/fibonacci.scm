(define (fibonacci n)
  (let loop ((i 1)
	     (x 1)
	     (y 1))
    (if (= i n)
	x
	(loop (+ i 1) y (+ x y)))))
(fibonacci 10)
