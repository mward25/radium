(provide 'common2.scm)

(define (keep func list)
  (if (null? list)
      '()
      (if (func (car list))
          (cons (car list)
                (keep func (cdr list)))
          (keep func (cdr list)))))


;;(keep (lambda (x) (= x 1)) (list 1 3 1 5))


(define-match keyvalues-to-define-args
  ()                 :> '()
  (Key)              :> (cons (list (keyword->symbol Key) ''must-be-defined)
                              '())
  (Key1 Key2 . Rest) :> (cons (list (keyword->symbol Key1) ''must-be-defined)
                              (keyvalues-to-define-args (cons Key2 Rest)))
                        :where (keyword? Key2)                   
  (Key Value . Rest) :> (cons (list (keyword->symbol Key) Value)
                              (keyvalues-to-define-args Rest)))

#|
(test (keyvalues-to-define-args '(:a 90 :b 50 :c :d 80))
      '((a 90) (b 50) (c 'must-be-defined) (d 80)))
(test (keyvalues-to-define-args '(:a 90 :b 50 :c))
      '((a 90) (b 50) (c 'must-be-defined)))
|#


(define-match delafina-args-to-define*-args
  ()           :> '()
  (Key . Rest) :> (keyvalues-to-define-args (cons Key Rest)) :where (keyword? Key)
  (Var . Rest) :> (cons Var (delafina-args-to-define*-args Rest)))

(define-macro (delafina def . body)
  `(define* (,(car def) ,@(delafina-args-to-define*-args (cdr def)))
     ,@body))

#|
(pretty-print (macroexpand (delafina (testfunc a b :b 30 :c 90)
                             (+ 2 3)
                             (+ 5 6))))

(test (macroexpand (delafina (testfunc a b :c 30 :d 90) a b))
      '(define* (testfunc a b (c 30) (d 90)) a b))
|#


(define-macro (define-struct name . args)
  (define define-args (keyvalues-to-define-args args))
  (define keys (map car define-args))
  (define must-be-defined (keep (lambda (arg)
                                  (equal? ''must-be-defined (cadr arg)))
                                define-args))
  (define table (gensym "table"))
  `(define* (,(<_> 'make- name) ,@(keyvalues-to-define-args args))
     ,@(map (lambda (must-be-defined)
              `(if (eq? ,(car must-be-defined) 'must-be-defined)
                   (throw ,(<-> "key '" (car must-be-defined) "' not defined when making struct '" name "'"))))
            must-be-defined)
     (let* ((,table (make-hash-table 32 eq?))
            (keys (quote ,keys)))
       ,@(map (lambda (key)
                `(hash-table-set! ,table ,(symbol->keyword key) ,key))
              keys)
       (lambda (key)
         (let ((ret (,table key)))
           (if (and (not ret)
                    (not (memq key keys)))
               (throw (<-> "key '" key ,(<-> "' not found in struct '" name "'")))
               ret))))))

#|
(pretty-print (macroexpand (define-struct teststruct
                             :a 'asdf
                             :b
                             :c #f)))

(pretty-print (macroexpand
               (define-struct test
                 :b 59
                 :c)))

           
(define-struct test
  :b 59
  :c)

(make-test :b 32)

(define t (make-test :c 2))
(t :b)
(t :c)
(t :bc)

|#

