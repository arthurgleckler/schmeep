(define-library (schmeep exception-formatter)
  (import (chibi)
	  (chibi ast))
  (export format-exception)
  (begin
    (define (format-irritant irritant)
      "Recursively format irritant, handling nested exceptions."
      (if (exception? irritant)
          (format-exception irritant "")
          irritant))
    (define (format-exception exception prefix)
      "Format a Scheme exception object into a human-readable error message."
      (let ((message (exception-message exception))
            (kind (exception-kind exception))
            (irritants (exception-irritants exception)))
        (call-with-output-string
	 (lambda (port)
	   (let ((components
		  (append '("Exception:")
			  (if message (list message) '())
			  (if kind `((kind ,kind)) '())
			  (cond ((not irritants) '())
				((null? irritants) '())
				((exception? irritants)
				 `((irritants
				    ,(format-irritant irritants))))
				((pair? irritants)
				 `((irritants
				    ,(map format-irritant irritants))))
				(else (list irritants))))))
	     (do ((c components (cdr c)))
		 ((null? c))
	       (display (car c) port)
	       (if (not (null? (cdr c))) (display " " port))))))))))