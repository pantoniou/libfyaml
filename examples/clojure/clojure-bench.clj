;; YAML benchmark for Clojure
(require '[clj-yaml.core :as yaml])

(let [filename (first *command-line-args*)]
  (when-not filename
    (println "Usage: clj clojure-bench.clj <yaml-file>")
    (System/exit 1))
  
  ;; Parse
  (let [start (System/nanoTime)
        content (slurp filename)
        data (yaml/parse-string content)
        parse-time (/ (- (System/nanoTime) start) 1e6)
        
        ;; Emit
        start2 (System/nanoTime)
        output (yaml/generate-string data)
        emit-time (/ (- (System/nanoTime) start2) 1e6)]
    
    ;; Print to stderr
    (.println System/err (format "Parse: %.2f ms" parse-time))
    (.println System/err (format "Emit:  %.2f ms" emit-time))
    (.println System/err (format "Total: %.2f ms" (+ parse-time emit-time))))
  
  (System/exit 0))
