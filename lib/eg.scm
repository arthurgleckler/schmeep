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
              (quoted-expr (cons 'begin expr-list))
              (port (open-output-string))
	      (result
               (parameterize ((current-output-port port))
                 (call-with-current-continuation
                  (lambda (k)
                    (with-exception-handler k
                      (lambda ()
                        (eval quoted-expr (interaction-environment))))))))
	      (output-str (get-output-string port))
              (expr-sxml `(code ,(call-with-output-string
				  (lambda (p) (write quoted-expr p)))))
              (result-sxml (if (exception? result)
                               `(span (@ (class "error"))
				      ,(format-exception result ""))
                               `(code ,(call-with-output-string
					(lambda (p) (write result p))))))
              (sxml (if (string=? output-str "")
                        `(li ,expr-sxml " ⇒ " ,result-sxml)
                        `(li ,expr-sxml
			     (br)
			     (span (@ (class "output")) ,output-str)
			     " ⇒ "
			     ,result-sxml))))
         (rax-response "#scheme-content ul" "append" (sxml->xml sxml)))))))

(define (eg-clear event-json)
  (rax-response "#scheme-content ul" "replace" "<ul></ul>"))