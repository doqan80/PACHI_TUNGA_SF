--[[
	Myte2 Server Files
	PACHI | Tunga
	https://forum.turkmmo.com/uye/2127751-pachi/
--]]
quest blood_stone begin
    state start begin
        when 20095.chat."Ma�aran�n ��i" begin
            local delay = 86400
            if get_global_time() - pc.getqf("time") < delay and pc.getqf("time") != 0 then
                local zeit = delay - (get_global_time() - pc.getqf("time"))
                say("Kan Ta��n� sana g�nde sadece bir kez yapabilirim!")
                say("Kalan s�re "..zeit.." saniye!")
                return
            end
            if pc.get_level() < 75 then
                say("Kan Ta�� kazanmak i�in �ok zay�fs�n!")
                say_reward("Seviyen 75'e ula�t���nda tekrar gel!")
                return
            end
            say("Merhaba!")
            say("Kap�dan ge�mek i�in Kan Ta��'na m� ihtiyac�n var?")
            say("�yleyse Ate� Diyar�ndan yedi")
            say("Fosil G�zya�� ve d�rt par�a Obsidyen getir!")
            local s= select("Hadi Gidelim!","Hay�r, Te�ekk�rler.")
            if s==1 then
                say_title("Ma�aran�n ��i")
                say("Ate� Diyar�na git ve Ejderha Tanr� Baljit")
                say("Elvedin'in Fosil G�zya�lar�n� bul. Ayn� ")
                say("zamanda Alevleri �ld�rerek Obsidyen")
                say("toplamal�s�n.")
                say("")
                say_reward("Gereken Fosil G�zya��: 7")
                say_reward("Gereken Obsidyen: 4")
                setstate(vulkangas)
            else
                say("�yi bir karar!")
                say("G�r���r�z..")
            end
        end
    end
    state vulkangas begin
        when letter begin
            send_letter("Obsidyen Ara")
        end
        when info or button begin
            say_title("Ma�aran�n ��i")
            say("4 adet Obsidyen toplamal�s�n.")
            say("Obsidyenleri Alevlerden elde edebilirsin.")
        end
        when kill begin
		if npc.get_race() == 2204 then
            local pct = number(1,20)
            if pct == 1 then
                pc.give_item2(30188,1)
        	  send_letter("Bu alevden Obsidyen buldun!")
            end
			end
            if pc.count_item(30188) >= 4 then
        	send_letter("Gereken t�m Obsidyeni toplad�n!")
                say("Tebrikler!")
                say("�imdi S�rada Fosil G�zya�� var!")
                say("Haritada beliren koordinatlardan toplamal�s�n.")
                setstate(vertrnen)
        end
        end
    end
    state vertrnen begin
        when letter begin
            send_letter("Fosil G�zya�lar�n� topla!")
                    local tropfen = {
                        { 1333, 785},
                        { 900,730},
                        { 661,717},
                        { 543,821},
                        { 460,771},
                        { 379,685},
                        { 480,623},
                        { 428,468},
                        { 327,359},
                        { 224,539},
                        { 175,727},
                        { 145,900},
                        { 69,1000},
                        { 176,1150},
                        { 438,1112},
                        { 646,1015},
                        { 790,1183},
                        { 1028,1052},
                        { 1326, 1028},
                        { 1376,1198},
                        { 1301,1388},
                        { 903,1399},
                        { 539, 1338},
                        { 574, 361},
                        { 846, 455},
                        { 1154, 498},
                        { 1222, 440},
                        { 1425,500},
                        { 1283,347},
                        { 1430,123},
                        { 1100,159},
                        { 787, 1083},
                        { 610,175},
                        { 500, 278},
                        { 390,125},
                }
                local r =  number(1,30)
                target.pos("p1", tropfen[r][1], tropfen[r][2], 62, "Fosil G�zya�� ")
                local r =  r+1
                target.pos("p2", tropfen[r][1], tropfen[r][2], 62, "Fosil G�zya�� ")
                local r =  r+1
                target.pos("p3", tropfen[r][1], tropfen[r][2], 62, "Fosil G�zya�� ")
                local r =  r+1
                target.pos("p4", tropfen[r][1], tropfen[r][2], 62, "Fosil G�zya�� ")
                local r =  r+1
                target.pos("p5", tropfen[r][1], tropfen[r][2], 62, "Fosil G�zya�� ")
                local r =  r+1
                target.pos("p6", tropfen[r][1], tropfen[r][2], 62, "Fosil G�zya�� ")
                local r =  r+1
                target.pos("p7", tropfen[r][1], tropfen[r][2], 62, "Fosil G�zya�� ")
                local r =  r+1
                pc.setqf("tr�nen",7)
        end
        when button or info begin
            say_title("Ma�aran�n ��i")
            say("�imdi 7 tane Fosil G�zya��na ihtiyac�n var!")
            say("Harita �zerindeki i�aretli noktalar� bul.")
        end
        when p1.target.arrive begin
        target.delete("p1")
        say_title("Fosil G�zya�� ")
        say("Burada Ejderha Tanr�'n�n yery�z�ne d��en")
        say("G�zya�lar�ndan biri var.")
        pc.give_item2(30186,1)
        pc.setqf("tr�nen",pc.getqf("tr�nen")-1)
        if pc.getqf("tr�nen") == 0 then
        send_letter("Yeteri kadar g�zya�� toplad�n!")
            setstate(rw)
        end
        end
        when p2.target.arrive begin
        target.delete("p2")
        say_title("Fosil G�zya�� ")
        say("Burada Ejderha Tanr�'n�n yery�z�ne d��en")
        say("G�zya�lar�ndan biri var.")
        pc.give_item2(30186,1)
        pc.setqf("tr�nen",pc.getqf("tr�nen")-1)
        if pc.getqf("tr�nen") == 0 then
        send_letter("Yeteri kadar g�zya�� toplad�n!")
            setstate(rw)
        end
        end
        when p3.target.arrive begin
        target.delete("p3")
        say_title("Fosil G�zya�� ")
        say("Burada Ejderha Tanr�'n�n yery�z�ne d��en")
        say("G�zya�lar�ndan biri var.")
        pc.give_item2(30186,1)
        pc.setqf("tr�nen",pc.getqf("tr�nen")-1)
        if pc.getqf("tr�nen") == 0 then
        send_letter("Yeteri kadar g�zya�� toplad�n!")
            setstate(rw)
        end
        end
        when p4.target.arrive begin
        target.delete("p4")
        say_title("Fosil G�zya�� ")
        say("Burada Ejderha Tanr�'n�n yery�z�ne d��en")
        say("G�zya�lar�ndan biri var.")
        pc.give_item2(30186,1)
        pc.setqf("tr�nen",pc.getqf("tr�nen")-1)
        if pc.getqf("tr�nen") == 0 then
        send_letter("Yeteri kadar g�zya�� toplad�n!")
            setstate(rw)
        end
        end
        when p5.target.arrive begin
        target.delete("p5")
        say_title("Fosil G�zya�� ")
        say("Burada Ejderha Tanr�'n�n yery�z�ne d��en")
        say("G�zya�lar�ndan biri var.")
        pc.give_item2(30186,1)
        pc.setqf("tr�nen",pc.getqf("tr�nen")-1)
        if pc.getqf("tr�nen") == 0 then
        send_letter("Yeteri kadar g�zya�� toplad�n!")
            setstate(rw)
        end
        end
        when p6.target.arrive begin
        target.delete("p6")
        say_title("Fosil G�zya�� ")
        say("Burada Ejderha Tanr�'n�n yery�z�ne d��en")
        say("G�zya�lar�ndan biri var.")
        pc.give_item2(30186,1)
        pc.setqf("tr�nen",pc.getqf("tr�nen")-1)
        if pc.getqf("tr�nen") == 0 then
        send_letter("Yeteri kadar g�zya�� toplad�n!")
            setstate(rw)
        end
        end
        when p7.target.arrive begin
        target.delete("p7")
        say_title("Fosil G�zya�� ")
        say("Burada Ejderha Tanr�'n�n yery�z�ne d��en")
        say("G�zya�lar�ndan biri var.")
        pc.give_item2(30186,1)
        pc.setqf("tr�nen",pc.getqf("tr�nen")-1)
        if pc.getqf("tr�nen") == 0 then
        send_letter("Yeteri kadar g�zya�� toplad�n!")
            setstate(rw)
        end
        end
    end
    state rw begin
        when letter begin
            send_letter("Ma�aran�n ��i")
        end
        when button or info begin
            say_title("Ma�aran�n ��i")
            say("G�zya�lar� ve Obsidyen ile Seon-Hae'ye d�n.")
            say("")
            say("Seon-Hae burada: 1. K�y")
        end
        when 20095.chat."Ma�aran�n ��i" begin
        if pc.count_item(30186) >= 7 and pc.count_item(30188)>= 4 then
        pc.remove_item(30186,77)
        pc.remove_item(30188,44)
        pc.give_item2(30189,1)
            say_title("Ma�aran�n ��i")
        say("i�te, vazoyu al ve Bahar-Taraji'nin sesini ara.")
        setstate(blut)
        else
        say("Obsidyen veya Fosil G�zya�� eksik!")
        end
    end
    end
    state blut begin
        when letter begin
            send_letter("Ma�aran�n ��i")
            tr�ne = {
            { 197, 214},
            { 326, 302},
            { 527, 90},
            { 616, 257},
            { 702, 262},
            { 674, 38},
        }
        local r2 =  number(1,6)
        target.pos("tr�ne", tr�ne[r2][1], tr�ne[r2][2], 68, "")
            end
        when info or button begin
            say_title("Ma�aran�n ��i")
            say("K�rm�z� Ormanda Bahar-Taraji'nin sesini ara.")
            say("Tanr��an�n ruhunu g�remesen bile, ruhunun")
            say("varl���n� hissedebileceksin.")
        end
        when tr�ne.target.arrive begin
            target.delete("tr�ne")
            say_title("Ma�aran�n ��i")
            say("R�zgar sessiz bir f�s�lt�y� getiriyor:")
            say("G�venli bir yere git... Buarada konu�amay�z...")
            say("Hayaletle konu�mak i�in g�reve t�kla.")
            wait()
            if pc.count_item(30189) >0 then
                pc.remove_item(30189,1)
                pc.give_item2(30187,1)
            say_title("Ma�aran�n ��i")
                say("Uzaktan, �ld�r�len Tanr��a Bahar-Taraji'nin sesi")
                say("duyuluyor...")
                say("")
                say(""..pc.name.." Seni tan�yorum.")
                say("")
                say("Bana a�abeyimin g�zya�lar�n� getirdin ve �d�l")
                say("olarak kan�m� kazand�n, sava�maya devam")
                say("edebilirsin.")
                setstate(stein)
            else
                say("Tanr��a G�zya�lar� sende de�il!")
                say("Onu buldu�un zaman tekrar beni bul.")
                            tr�ne = {
            { 197, 214},
            { 326, 302},
            { 527, 90},
            { 616, 257},
            { 702, 262},
            { 674, 38},
        }
        local r2 =  number(1,6)
        target.pos("tr�ne", tr�ne[r2][1], tr�ne[r2][2], 68, "")
            end
        end
    end
    state stein begin
            when letter begin
                send_letter("Ma�aran�n ��i")
            end
            when button or info begin
            say_title("Ma�aran�n ��i")
                say("Seon-Hae'ye Bahar-Taraji'nin kan�n� g�t�r.!")
                say("")
                say("Seon-Hae burada : 1. K�y")
                say()
            end
            when 20095.chat."Ma�aran�n ��i" begin
                if pc.count_item(30187) <= 0 then
                    say("Kan Damlas� sende de�il!")
                else
                    pc.remove_item(30187,1)
                    pc.give_item2(30190,1)
            say_title("Ma�aran�n ��i")
                    say("Bahar-Taraji'nin kan�n� ta�a �evirece�im. ��ine!")
                    say("yarayacakt�r.")
                    say("")
                    say_reward("Sana g�nde sadece bir kez Kan Ta�� yapabilirim.")
                    say_reward("Aksi takdirde Koe-Pung ��phelenir.")
                    clear_letter()
                    pc.setqf("time",get_global_time())
                    setstate(start)
                end
            end
        end
    end 