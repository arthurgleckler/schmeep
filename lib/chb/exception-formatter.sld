(define-library (chb exception-formatter)
  (import (chibi)
	  (chibi ast))
  (export format-exception)
  (begin
    (define (format-exception exception prefix original-expression)
      "Format a Scheme exception object into a human-readable error message."
      (if (exception? exception)
          (let ((message (exception-message exception))
                (kind (exception-kind exception))
                (irritants (exception-irritants exception)))
            (call-with-output-string
	     (lambda (port)
	       (let ((components (append
				  (if message (list message) '())
				  (if kind `((kind ,kind)) '())
				  (if (and irritants (not (null? irritants)))
				      `((irritants ,@irritants))
				      '()))))
		 (do ((c components (cdr c)))
		     ((null? c))
		   (display (car c) port)
		   (if (not (null? (cdr c))) (display " " port)))))))
          "Not an exception."))))