--[[
	Myte2 Server Files
	PACHI | Tunga
	https://forum.turkmmo.com/uye/2127751-pachi/
--]]
quest dragon_charm begin
	state start begin
		function kontrol()
			if pc.is_busy() == true then return true end
			return false
		end
		when login begin
			if pc.get_map_index() == 72 or pc.get_map_index() == 73 or pc.get_map_index() == 79 then
				if pc.get_level() < 75 then
					chat("Seviyen 75'in alt�nda oldu�u i�in buraya giremezsin.")
					warp_to_village()
				end
			end
		end
		when 30180.use begin
			if pc.count_item(30180) < 1 then 
				say_title("Bug Kontrol") 
				say("Envanterindeki Ejderha Tanr� T�ls�m� kaybolmu�.") 
				return 
			end
			if pc.get_level() < 75 then syschat("Seviyen yeterli de�il.") return end
			if pc.get_map_index() == 72 and not pc.is_gm() then
				say_title("Ejderha Tanr� T�ls�m� ")
				say("")
				say("E�er gitmek istedi�in yer S�rg�n Ma�aras�'n�n")
				say("2. kat�nda bulunuyorsa, �ncelikle 2. kata")
				say("ge�men gerekiyor. Orada t�ls�m� kulland���n")
				say("zaman farkl� se�enekler ��kacak.")
				say("")
				say_reward("Birini se�.")
				local s = select("Karanl�k Tap�nak","Sura �skeletleri","Ma�aran�n sonu","Vazge� ")
				if s == 1 then
					say_title("Ejderha Tanr� T�ls�m� ")
					say("")
					say("Karanl�k Tap�nak yak�nlar�na ���nlan�yorsun...")
					pc.remove_item("30180",1)
					wait()
					pc.warp(82900,1286200)
				elseif s == 2 then
					say_title("Ejderha Tanr� T�ls�m� ")
					say("Birini se�.")
					local a = select("Sura �skeleti-1","Sura �skeleti-2","Sura �skeleti-3","Sura �skeleti-4","Vazge� ")
					if a == 1 then
						say_title("Ejderha Tanr� T�ls�m� ")
						say("")
						say("Sura �skeleti-1 yak�nlar�na ���nlan�yorsun...")
						pc.remove_item("30180",1)
						wait()
						pc.warp(20800,1290300)
					elseif a == 2 then
						say_title("Ejderha Tanr� T�ls�m� ")
						say("")
						say("Sura �skeleti-2 yak�nlar�na ���nlan�yorsun...")
						pc.remove_item("30180",1)
						wait()
						pc.warp(11800,1334300)
					elseif a == 3 then
						say_title("Ejderha Tanr� T�ls�m� ")
						say("")
						say("Sura �skeleti-3 yak�nlar�na ���nlan�yorsun...")
						pc.remove_item("30180",1)
						wait()
						pc.warp(48300,1340700)
					elseif a == 4 then
						say_title("Ejderha Tanr� T�ls�m� ")
						say("")
						say("Sura �skeleti-4 yak�nlar�na ���nlan�yorsun...")
						pc.remove_item("30180",1)
						wait()
						pc.warp(42000,1268200)
					elseif a == 5 then
						return
					end
				elseif s == 3 then
					say_title("Ejderha Tanr� T�ls�m� ")
					say("")
					say("Ma�aran�n 1. kat�n�n sonuna ���nlan�yorsun...")
					pc.remove_item("30180",1)
					wait()
					pc.warp(131900,1343700)
				elseif s == 4 then
					return
				end
				
			elseif pc.get_map_index() == 73 and not pc.is_gm() then
				say_title("Ejderha Tanr� T�ls�m� ")
				say("")
				say("S�rg�n Ma�aras� 2. kattas�n. Gidebilece�in")
				say("yerler a�a��da. E�er gitmek istedi�in yer")
				say("1. katta ise �nce 1. kata d�nmelisin.")
				say("")
				say_reward("Birini se�.")
				local ss = select("Komutanlar","Sura Ruhu","Sura �skeleti-5","Vazge� ")
				if ss == 1 then
					say_title("Ejderha Tanr� T�ls�m� ")
					say("Birini se�.")
					local aa = select("Komutan","General (1)","General (2)","General (3)","Vazge� ")
					if aa == 1 then
						say_title("Ejderha Tanr� T�ls�m� ")
						say("")
						say("Komutan�n yak�nlar�na ���nlan�yorsun...")
						pc.remove_item("30180",1)
						wait()
						pc.warp(195800,1220600)
					elseif aa == 2 then
						say_title("Ejderha Tanr� T�ls�m� ")
						say("")
						say("General (1) yak�nlar�na ���nlan�yorsun...")
						pc.remove_item("30180",1)
						wait()
						pc.warp(279600,1260800)
					elseif aa == 3 then
						say_title("Ejderha Tanr� T�ls�m� ")
						say("")
						say("General (2) yak�nlar�na ���nlan�yorsun...")
						pc.remove_item("30180",1)
						wait()
						pc.warp(191100,1233400)
					elseif aa == 4 then
						say_title("Ejderha Tanr� T�ls�m� ")
						say("")
						say("General (3) yak�nlar�na ���nlan�yorsun...")
						pc.remove_item("30180",1)
						wait()
						pc.warp(229700,1317600)
					elseif aa == 5 then
						return
					end
				elseif ss == 2 then
					say_title("Ejderha Tanr� T�ls�m� ")
					say("")
					say("Sura Ruhu yak�nlar�na ���nlan�yorsun...")
					pc.remove_item("30180",1)
					wait()
					pc.warp(180100,1220400)
				elseif ss == 3 then
					say_title("Ejderha Tanr� T�ls�m� ")
					say("")
					say("Sura �skeleti-5 yak�nlar�na ���nlan�yorsun...")
					pc.remove_item("30180",1)
					wait()
					pc.warp(251600,1296000)
				elseif ss == 4 then
					return
				end
			elseif pc.is_gm() then
				local aa = select("Ma�aran�n sonu","Sura Ruhu","Komutan","General (1)","General (2)","General (3)","Vazge� ")
					if aa == 1 then
						say_title("Ejderha Tanr� T�ls�m� ")
						say("")
						say("Ma�aran�n 1. kat�n�n sonuna ���nlan�yorsun...")
						wait()
						pc.warp(131900,1343700)
					elseif aa == 2 then
						say_title("Ejderha Tanr� T�ls�m� ")
						say("")
						say("Sura Ruhu yak�nlar�na ���nlan�yorsun...")
						wait()
						pc.warp(180100,1220400)
					elseif aa == 3 then
						say_title("Ejderha Tanr� T�ls�m� ")
						say("")
						say("Komutan�n yak�nlar�na ���nlan�yorsun...")
						wait()
						pc.warp(195800,1220600)
					elseif aa == 4 then
						say_title("Ejderha Tanr� T�ls�m� ")
						say("")
						say("General (1) yak�nlar�na ���nlan�yorsun...")
						wait()
						pc.warp(279600,1260800)
					elseif aa == 5 then
						say_title("Ejderha Tanr� T�ls�m� ")
						say("")
						say("General (2) yak�nlar�na ���nlan�yorsun...")
						wait()
						pc.warp(191100,1233400)
					elseif aa == 6 then
						say_title("Ejderha Tanr� T�ls�m� ")
						say("")
						say("General (3) yak�nlar�na ���nlan�yorsun...")
						wait()
						pc.warp(229700,1317600)
					elseif aa == 7 then
						return
					end
			else
				say_title("Ejderha Tanr� T�ls�m� ")
				say("")
				say("T�ls�m� kullanmak i�in S�rg�n Ma�aras� ")
				say("i�erisinde olman gerekiyor.")
				say("")
			end
		end
	end
end