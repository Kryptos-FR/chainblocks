(def Root (Node))

(def test
  (Chain
   "test"
                                        ; params
   1 (ToString)
   (Set .params "postId")
                                        ; GET
   .params
   (Http.Get "jsonplaceholder.typicode.com" "/comments")
   (FromJson)
   (ExpectSeq)
   (Take 0)
   (ExpectTable)
   (Take "name")
   (Assert.Is "id labore ex et quam laborum" true)
   (Log)
                                        ; POST
   .params
   (Http.Post "jsonplaceholder.typicode.com" "/posts") &> .json
   (FromJson)
   (ExpectTable)
   (Take "id")
   (Assert.Is 101 true)
   (Log)
   
   .json
   (Http.Post "postman-echo.com" "/post")
   (Log)

  ;;  nil (Http.Get "localhost" "/" :Port 8000 :Secure false)
  ;;  (Log)
   ))

(schedule Root test)
(run Root 0.1)

; test reusing sockets/streams
(schedule Root test)
(run Root 0.1)

;; (def server-handler
;;   (Chain
;;    "server-handler"
;;    :Looped
;;    (Msg "handled")
;;    (Http.Read)
;;    (Log)
;;    1 >> .r
;;    2 >> .r
;;    3 >> .r
;;    .r (ToJson) (Http.Response)))

;; (def test-server
;;   (Chain
;;    "test"
;;    :Looped
;;    (Http.Server :Handler server-handler)))

;; (schedule Root test-server)
;; (run Root 0.1)