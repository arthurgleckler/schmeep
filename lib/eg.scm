(import (scheme base))
(import (chibi json))
(import (chibi sxml))
(import (schmeep exception-formatter))

(define (rax-response selector verb html)
  (let ((response-alist
         `((,(string->symbol selector) . ((verb . ,verb)
                                          (html . ,html))))))
    (json->string response-alist)))

(define-syntax eg
  (syntax-rules ()
    ((eg expression ...)
     (lambda (event-json)
       (let* ((expr-list '(expression ...))
              (port (open-output-string))
	      (result
               (parameterize ((current-output-port port))
                 (call-with-current-continuation
                  (lambda (k)
                    (with-exception-handler k
                      (lambda ()
                        (eval (cons 'begin expr-list)
			      (interaction-environment))))))))
	      (output (get-output-string port))
              (expr-sxml `(code ,(call-with-output-string
				  (lambda (p)
				    (for-each (lambda (x) (write x p))
					      expr-list)))))
              (result-sxml (if (exception? result)
                               `(span (@ (class "error"))
				      ,(format-exception result ""))
                               `(code ,(call-with-output-string
					(lambda (p) (write result p))))))
              (sxml (if (string=? output "")
                        `(li ,expr-sxml " ⇒ " ,result-sxml)
                        `(li ,expr-sxml
			     (br)
			     (span (@ (class "output")) ,output)
			     " ⇒ "
			     ,result-sxml))))
         (rax-response "#scheme-content ul" "append" (sxml->xml sxml)))))))

(define (eg-clear event-json)
  (rax-response "#scheme-content ul" "replace" "<ul></ul>"))