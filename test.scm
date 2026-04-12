(display (let ((x 1)) 
  (let ((f (lambda () x)) (x 2)) 
  (f))))