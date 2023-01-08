#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <sys/wait.h>       

//====================================================================
//==================Proses Başlangıcı ================================
//====================================================================
// Belirli bir proses için bağımsız değişken boyutu
#define MAX_ARGS            3


#define DEFAULT_PROCESS     " "

//Proses durumları
#define PCB_UNINITIALIZED   0
#define PCB_INITIALIZED     1
#define PCB_READY           2
#define PCB_RUNNING         3
#define PCB_SUSPENDED       4
#define PCB_TERMINATED      5

/*
** PCB struct, her yeni proses için proses kontrol bloğunu temsil eder. Prosese ait birkaç veri içerir.
** Bunlar, proses id, varış zamanı, öncelik, proses zamanı, proses durumu 
** ve bir sonraki prosesi gösteren bir işaretçi (eğer proses hala kuyruktaysa)
*/
struct PCB
{
    pid_t pid;                             // proses id
    char* args[MAX_ARGS];                 // proses  bağımsız değişkenleri
    unsigned int varis_zamani;           // Proses, kuyruğa vardığı zaman
    unsigned int oncelik;               // proses önceliği (0 gerçek zamanlı proses için, 1-3 Kullanıcı Prosesi için) 
    unsigned int proses_zamani;        // prosesin tamamlanmasına kalan süre
    unsigned int proses_durumu;       // prosesin mevcut durumu
    struct PCB* sonraki;             // bir sonraki prosesi gösteren bir işaretçi (eğer proses hala kuyruktaysa)
};
struct PCB* bos_PCB_olustur();          //Tüm değerler NULL/0 olarak başlatılmış olarak PCB yapısına yeni bir işaretçi oluşturuldu.
struct PCB* PCB_Baslat(struct PCB* P);  // bir prosesi başlatır
struct PCB* PCB_askiya_al(struct PCB* P); // Bir prosesi askıya alır
struct PCB* PCB_tekrar_Baslat(struct PCB* P); // Önceden askıya alınmış bir prosesi yeniden başlatır
struct PCB* PCB_sonlandir(struct PCB* P);   // Bir prosesi sonlandırır
struct PCB* PCB_enqueue(struct PCB* ilk, struct PCB* P);  // Kuyruğa bir porses ekler
struct PCB* PCB_dequeue(struct PCB* ilk);  // Bir prosesi kuyruğunun başından kaldırır
//====================================================================
//==================Proses Bitişi ==================
//====================================================================

//====================================================================
//==================kuyruk Başlangıcı ==============
//====================================================================

// Global değişkenler
struct PCB* giris_kuyrugu = NULL;
struct PCB* gercek_zamanli_kuyrugu = NULL;	
struct PCB* kullanici_proses_kuyrugu = NULL;
struct PCB* oncelik_bir_kuyrugu = NULL;	
struct PCB* oncelik_iki_kuyrugu = NULL;	
struct PCB* oncelik_uc_kuyrugu = NULL;	
struct PCB* mevcut_proses = NULL;	// Şu anda çalışan prosesin işaretçisi
struct PCB* proses = NULL;	//Yeni proses oluştururken kullanılacak yardımcı proses işaretçisi
float timer = 0;	// Tüm sistem için zamanlayıcı

void giris_kuyrugu_doldur(char* giris_dosyasi, FILE* giris_listenin_akisi);	// Giriş dosyasına göre giriş kuyruğunu doldur
bool tamamlandiMi();	// Sistemin tüm prosesleri tamamlayıp tamamlamadığını kontrol et
void giris_kuyrugu_kontrol();	// prosesleri  giriş kuyruğundan kaldır ve uygun kuyruğa koy
void kullanici_proses_kuyrugu_kontrol();	//prosesleri kullanıcı proses kuyruğundan kaldır ve uygun kuyruğa yerleştir
void mevcut_prosesi_kontrol();	//Mevcut proses üzerinde işlem yap. Gerekirse proses sonlandır yada askıya al.
void mevcut_prosesi_tahsis();	// Hala tamamlanması gereken prosesler varsa

//====================================================================
//==================kuyruk Bitişi ==================
//====================================================================
/*
** fonksiyon: main()
** ---------------------------------
** Dağıtım sistemini çalıştıran temel mantığı içerir. 
** Öncelikle proses listesi içeren dosyayı açarak tüm peosesler tamamlanana kadar döngü yaparak tüm bu prosesleri yönetir.
** Tüm prosesler uygun kuyruklara yerleştirilir, ardından doğru sırayla çalışır.
*/
int main(int argc, char* argv[])
{
     
    FILE* giris_listenin_akisi;        

    if (!(giris_listenin_akisi = fopen("giris.txt","r")))	// Dosyayı aç
    {
        // Dosyayı açarken bir hata oluştuysa, hata yazdırın ve programdan çıkın.
        printf("Dosya acilirken hata olustu");	
        exit(0);
    }
    
    // Giriş kuyruğunu yeni proseslerle doldurun
    giris_kuyrugu_doldur("giris.txt", giris_listenin_akisi);	

    
    while (!tamamlandiMi())	// Tüm proses bitene kadar çalışır
    {
        giris_kuyrugu_kontrol();	// prosesleri  giriş kuyruğundan kaldır ve uygun kuyruğa koy
        
	    kullanici_proses_kuyrugu_kontrol();	// Prosesleri kullanıcı iş kuyruğundan kaldır ve uygun kuyruğa koy

        if (mevcut_proses)    // Şu anda çalışan bir proses var, bir kaç işlem yapılacak
        {
            mevcut_prosesi_kontrol();
        }
        // Hala tamamlanması gereken prosesler var
        if ((gercek_zamanli_kuyrugu || oncelik_bir_kuyrugu || oncelik_iki_kuyrugu || oncelik_uc_kuyrugu) && (!mevcut_proses))	
        {
            mevcut_prosesi_tahsis();
        }

        sleep(1);	
        timer += 1;	//Geçerli zamana 1 saniye ekleyin
    }

    printf("\nTüm prosesler bitmiştir.\n");

    return 0 ;
}
/*
** fonksiyon: bos_PCB_olustur()
** ---------------------------------
** Tüm değerler NULL/0 olarak başlatılmış olarak PCB yapısına yeni bir işaretçi oluşturuldu.
*/

struct PCB* bos_PCB_olustur()
{
    struct PCB* P = (struct PCB*)malloc(sizeof(struct PCB));  //Yeni PCB oluştur
    P->pid = 0;
    P->args[0] = DEFAULT_PROCESS ;           
    P->args[1] = NULL;                      
    P->varis_zamani = 0;
    P->oncelik = 0;
    P->proses_zamani = 0;
    P->proses_durumu = PCB_UNINITIALIZED;    
    P->sonraki = NULL;                      
 
    return P;
}
/*
** fonksiyon: PCB_Baslat()
** ---------------------------------
** fork() kullanarak prosesi oluşturup ilk kez çalıştırır.
** Ek olarak PCB'nin durumunu güncellenir.
**
** giris: struct PCB* - başlatılacak proses için bir işaretçi
** cikis: struct PCB* - yeni başlayan proses için bir işaretçi
*/
struct PCB* PCB_Baslat(struct PCB* P)
{
    if(P->pid == 0)     //prosesin zaten başlatılmadığından emin olun
    {
        switch(P->pid = fork()) // prosesi çatallayarak başlatın
        {
            case -1:    // işleme hatası (proses doğru şekilde çatallanmadığında)
                exit(1);
            case 0: 
                P->pid = getpid();  
                P->proses_durumu = PCB_RUNNING; 
                printf("\n %f  sn proses başladı       (id:%d  oncelik:%d  kalan sure:%d sn).\n",timer, P->pid,P->oncelik,P->proses_zamani);
                fflush(stdout);                         
		        execvp(P->args[0], P->args);           
                exit(2);
        }
    }
    else               //proses zaten başlatılmışsa,  sinyali göndererek yeniden çalıştırmanız yeterlidir.
    {
        kill(P->pid, SIGCONT);                    
    }
    P->proses_durumu = PCB_RUNNING;
    
    return P;
}
/*
** fonksiyon: PCB_askiya_al()
** ---------------------------------
** Daha önce başlatılmış olan bir prosesi askıya alır.
** in: struct pcb* - çalışan prosesin işaretçisi
** out: struct pcb* - yeni askıya alınan prosesin işaretçisi
*/
struct PCB* PCB_askiya_al(struct PCB* P)
{
    int durum;
    kill(P->pid, SIGTSTP);                        // prosese  bir durdurma sinyali gönder  
    waitpid(P->pid, &durum, WUNTRACED);          // prosesten dönüşü bekleyin              
    P->proses_durumu = PCB_SUSPENDED;           // PCB durumunu güncelle
    printf("\n %f  sn proses askıda        (id:%d  oncelik:%d  kalan sure:%d sn).\n",timer, P->pid,P->oncelik,P->proses_zamani);
    return P;
}

/*
** fonksiyon: PCB_tekrar_Baslat()
** ---------------------------------
** Önceden askıya alınmış bir prosesi yeniden başlatır
*/
struct PCB* PCB_tekrar_Baslat(struct PCB* P)
{
    kill(P->pid, SIGCONT);  // proses devam etmek için sinyali gönder   
    printf("\n %f  sn proses devam         (id:%d  oncelik:%d  kalan sure:%d sn).\n",timer, P->pid,P->oncelik,P->proses_zamani);
    return P;
}

/*
** fonksiyon: PCB_sonlandir()
** ---------------------------------
** Bir prosesi sonlandırır. prosesin çalışması bittiğinde çağrılacak.
*/
struct PCB* PCB_sonlandir(struct PCB* P)
{
    int durum;
    kill(P->pid, SIGINT);   // prosese sonlandırma sinyali gönder
    waitpid(P->pid, &durum, WUNTRACED);    // prosesten dönüşü bekleyin   
    P->proses_durumu = PCB_TERMINATED; // PCB durumunu güncelle
    printf("\n %f  sn proses sonlandı      (id:%d  oncelik:%d  kalan sure:%d sn).\n",timer, P->pid,P->oncelik,P->proses_zamani);
    return P;
}

/*
** fonksiyon: PCB_enqueue()
** ---------------------------------
** Kuyruğun sonuna bir proses ekler
*/
struct PCB* PCB_enqueue(struct PCB* ilk, struct PCB* P)
{
    struct PCB* gec = ilk;
    if (ilk)   // Kuyruk şu anda boş değilse
    {
        while (gec->sonraki)    // Kuyruğun sonunu bul
        {
            gec = gec->sonraki;  
        }
        gec->sonraki = P;   // prosesi kuyruğun sonuna ekle
        return ilk;
    }
    else    // Kuyruk boşsa, sadece P'yi döndür
    {
        return P;
    }
}

/*
** fonksiyon: dequeue_pcb()
** ---------------------------------
** Bir prosesi kuyruğunun başından kaldırır.
*/
struct PCB* PCB_dequeue(struct PCB* ilk)
{
    if (!ilk)
    {
	    return NULL;    // Kuyruk boşsa NULL döndür
    }
    else
    {
        struct PCB* gec = ilk->sonraki;   // ilk düğümü kaldır
        ilk->sonraki = NULL;
	    return gec; // Kuyrukta yeni ilk düğümü döndür
    }
}

//====================================================================
//==================Proses Bitişi ==================
//====================================================================

//====================================================================
//==================kuyruk Başlangıcı ==============
//====================================================================

/*
** fonksiyon: giris_kuyrugu_doldur()
** ---------------------------------
*/
void giris_kuyrugu_doldur(char* giris_dosyasi, FILE* giris_listenin_akisi)
{
    struct PCB* proses;	// Oluşturulacak yeni prosesin işaretçisi
    char satir[50];	// satir buffer

    while (fgets(satir, 50, giris_listenin_akisi))	// Dosyayı satır satır dolaş
    {
	if (satir[0] == '\n')	// Dosya bittiğinde döngüden çık
	{
	    break;
	}
	
        proses = bos_PCB_olustur();	// Boş proses oluştur

        char* s = strtok(satir, ",");	// prosesin üç parametresi için satırı ayrıştırın
        int nums[3];
        int i = 0;

        while (s)
        {
            int n = atoi(s);
            s = strtok(NULL, ",");
            nums[i] = n;
            ++i;
        }
        //Porsesin bilgileri
        proses->varis_zamani = nums[0];
        proses->oncelik = nums[1];
        proses->proses_zamani = nums[2];

        giris_kuyrugu = PCB_enqueue(giris_kuyrugu, proses);	// Giriş kuyruğuna proses ekle

        proses = NULL;
    }

    fclose(giris_listenin_akisi);	// dosyayı kapat
}

/*
** tamamlandiMi()
** ---------------------------------
** Tüm kuyrukların boş olup olmadığını kontrol ederek sistemin tüm prosesleri tamamlayıp tamamlamadığını kontrol eder.
*/
bool tamamlandiMi()
{
    return (!giris_kuyrugu && !gercek_zamanli_kuyrugu && !kullanici_proses_kuyrugu && !oncelik_bir_kuyrugu 
        && !oncelik_iki_kuyrugu && !oncelik_uc_kuyrugu && !mevcut_proses);
}

/*
** fonksiyon: giris_kuyrugu_kontrol()
** ---------------------------------
**Giriş kuyruğunu  kontrol edilir. Giriş kuyruğunda, varış zamanı şimdiki zamana eşit olan herhangi bir proses varsa,
**onlar Giriş kuyruğundan çıkarılır ve uygun kuyruğa yerleştirilir.
*/
void giris_kuyrugu_kontrol()
{
    while (giris_kuyrugu && giris_kuyrugu->varis_zamani <= timer)	
    {
        struct PCB* P = giris_kuyrugu;
	    giris_kuyrugu = PCB_dequeue(giris_kuyrugu);	// Giriş kuyruğundan kaldır
        if (P->oncelik== 0)	
        {
            gercek_zamanli_kuyrugu = PCB_enqueue(gercek_zamanli_kuyrugu, P);	// Gerçek zamanlı kuyruğa yerleştir
        }
        else
        {
            kullanici_proses_kuyrugu = PCB_enqueue(kullanici_proses_kuyrugu, P); 	// Kullanıcı proses kuyruğuna yerleştir
        }
    }
}

/*
** fonksiyon: kullanici_proses_kuyrugu_kontrol()
** ---------------------------------
**Kullanıcı iş kuyruğunu kontrol edilir. 
**Kullanıcı proses kuyruğunda herhangi bir proses varsa prosesi uygun öncelik kuyruğuna yerleştirilir.
*/
void kullanici_proses_kuyrugu_kontrol()
{
    while(kullanici_proses_kuyrugu)  	// kullanıcı proses kuyruğunu kontrol et
    {
        struct PCB* P = kullanici_proses_kuyrugu;	
	    kullanici_proses_kuyrugu = PCB_dequeue(kullanici_proses_kuyrugu);	// Kullanıcı proses kuyruğundan kaldır
        switch(P->oncelik)	// Uygun öncelik kuyruğuna yerleştirin
        {
            case 1:
                oncelik_bir_kuyrugu = PCB_enqueue(oncelik_bir_kuyrugu, P);
                break;
            case 2:
                oncelik_iki_kuyrugu = PCB_enqueue(oncelik_iki_kuyrugu, P);
                break;
            default:
                oncelik_uc_kuyrugu = PCB_enqueue(oncelik_uc_kuyrugu, P);
                break;
        }
    }
}

/*
** fonksiyon: mevcut_prosesi_kontrol()
** ---------------------------------
**Geçerli işlemde kalan süreyi azaltın.Bittiyse, prosesi sonlandır.
**Bitmediyse, önceliği azalt ve uygun öncelik kuyruğuna yerleştir (eğer gerçek zamanlı bir proses değilse)
*/
void mevcut_prosesi_kontrol()
{
    if (--mevcut_proses->proses_zamani == 0) // prosesin bitip bitmediğini kontrol et
    {
        PCB_sonlandir(mevcut_proses);	// prosesi sonlandır
        free(mevcut_proses);
        mevcut_proses = NULL;
    }
    else    // proses bitmedi
    {   
        if(gercek_zamanli_kuyrugu &&(mevcut_proses->oncelik == 0))
        {
            printf("\n %f  sn proses yürütülüyor   (id:%d  oncelik:%d  kalan sure:%d sn).\n",timer, mevcut_proses->pid,mevcut_proses->oncelik,mevcut_proses->proses_zamani); 
        }
        if ((gercek_zamanli_kuyrugu || kullanici_proses_kuyrugu || oncelik_bir_kuyrugu || oncelik_iki_kuyrugu || oncelik_uc_kuyrugu) &&    
            (mevcut_proses->oncelik != 0))     // Kuyrukta hala proses var
        {
            struct PCB* P = PCB_askiya_al(mevcut_proses);	// Mevcut prosesi askıya al
            if (++P->oncelik > 3)	// prosesin önceliğini azaltın
            {
                P->oncelik = 3;
            }
            switch(P->oncelik)	// prosei uygun öncelik kuyruğna yerleştirin
            {
                case 1:
                    oncelik_bir_kuyrugu = PCB_enqueue(oncelik_bir_kuyrugu, P);
                    break;
                case 2:
                    oncelik_iki_kuyrugu = PCB_enqueue(oncelik_iki_kuyrugu, P);
                    break;
                default:
                    oncelik_uc_kuyrugu = PCB_enqueue(oncelik_uc_kuyrugu, P);
                    break;
            }
            mevcut_proses = NULL;	// 
        }
    }
}

/*
** fonksiyon: mevcut_prosesi_tahsis()
** ---------------------------------
**Hala tamamlanması gereken prosesler varsa
*/
void mevcut_prosesi_tahsis()
{
    if (gercek_zamanli_kuyrugu)	// Önce gerçek zamanlı kuyruğu kontrol et
    {
        mevcut_proses = gercek_zamanli_kuyrugu;
	    gercek_zamanli_kuyrugu = PCB_dequeue(gercek_zamanli_kuyrugu);
    }
    else if (oncelik_bir_kuyrugu)	// Ardından önceliği bir kuyruğu kontrol et
    {
        mevcut_proses = oncelik_bir_kuyrugu;
	    oncelik_bir_kuyrugu = PCB_dequeue(oncelik_bir_kuyrugu);
    }
    else if (oncelik_iki_kuyrugu)	// Ardından öncelik iki kuyruğunu kontrol et
    {
        mevcut_proses = oncelik_iki_kuyrugu;
	    oncelik_iki_kuyrugu = PCB_dequeue(oncelik_iki_kuyrugu);
    }
    else	// Aksi takdirde, öncelikli üç kuyruktan at
    {
        mevcut_proses = oncelik_uc_kuyrugu;
	    oncelik_uc_kuyrugu = PCB_dequeue(oncelik_uc_kuyrugu);
    }

    if (mevcut_proses->pid != 0)	//prosesin daha önce başlatılıp başlatılmadığını kontrol et
    {
        PCB_tekrar_Baslat(mevcut_proses);	// Eğer öyleyse, sadece yeniden başlat
    }
    else	// Aksi halde ilk defa proses başlatılıyor demektir.
    {
        PCB_Baslat(mevcut_proses);
    }
}
//====================================================================
//==================kuyruk Bitişi ==================
//====================================================================