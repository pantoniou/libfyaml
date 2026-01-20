;; Simple YAML benchmark for Clojure
(require '[clj-yaml.core :as yaml])

(let [filename (first *command-line-args*)]
  (when-not filename
    (println "Usage: clj clojure-simple.clj <yaml-file>")
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
    
    (println output)
    (binding [*out* *err*]
      (printf "Parse: %.2f ms\n" parse-time)
      (printf "Emit:  %.2f ms\n" emit-time)
      (printf "Total: %.2f ms\n" (+ parse-time emit-time))))
  
  ;; Explicitly exit
  (System/exit 0))
