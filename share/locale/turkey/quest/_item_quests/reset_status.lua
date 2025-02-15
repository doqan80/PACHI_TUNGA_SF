--[[
	Myte2 Server Files
	PACHI | Tunga
	https://forum.turkmmo.com/uye/2127751-pachi/
--]]
quest reset_status begin
	state start begin
		when 71103.use or 71104.use or 71105.use or 71106.use begin
			local name = { "VIT", "INT", "STR", "DEX" }
			local idx = item.get_vnum() - 71103
			local func = { pc.get_ht, pc.get_iq, pc.get_st, pc.get_dx }

			if func[idx+1]() == 1 then
				say(name[idx+1].." zaten 1.")
				say(name[idx+1].." daha a�a�� indiremem.")
				say("")
				return
			end
			say_title("S�f�rlama Durumu:")
			say("Bu e�yay� kullanarak")
			say(name[idx+1].." puan�n� 1 yapabilirsin.")
			say("")
			say_reward(""..name[idx+1].." puan�n 1 olacak emin misin?")

			local s = select("Evet", "Hay�r")

			if s == 1 then
				if pc.reset_status( idx ) == true then
					say_title("S�f�rlama Durumu:")
					say_reward(""..name[idx+1].." puan�n 1 e d���r�ld�!")
					say("")
					pc.remove_item(item.get_vnum())
				else
					say_title("S�f�rlama Durumu:")
					say_reward("Bir hata olu�tu. Tekrar dene.")
					say("")
				end
			end
		end
	end
end