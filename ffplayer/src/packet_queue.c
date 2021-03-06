#include "packet_queue.h"


extern int quit;

void packet_queue_init(PacketQueue *q)
{
	if(!q) return;

	packet_queue_clean(q);
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
}

void packet_queue_clean(PacketQueue *q)
{
	if(q)
	{
		if(q->mutex) 
		{
			SDL_DestroyMutex(q->mutex);
			q->mutex = NULL;
		}
		if(q->cond)
		{
			SDL_DestroyCond(q->cond);
			q->cond = NULL;
		}

		memset(q, 0, sizeof(PacketQueue));
	}
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt) 
{
	AVPacketList *pkt1;

	if(!q) return -1;
	if(!pkt) return -2;

	if(av_dup_packet(pkt) < 0) 
	{
		return -101;
	}
	pkt1 = av_malloc(sizeof(AVPacketList));
	if (!pkt1) return -102;
	pkt1->pkt = *pkt;
	pkt1->next = NULL;


	SDL_LockMutex(q->mutex);

	if (!q->last_pkt)
		q->first_pkt = pkt1;
	else
		q->last_pkt->next = pkt1;

	q->last_pkt = pkt1;

	q->nb_packets++;
	q->size += pkt1->pkt.size;
	SDL_CondSignal(q->cond);

	SDL_UnlockMutex(q->mutex);
	return 0;
}

int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
	AVPacketList *pkt1;
	int ret;

	SDL_LockMutex(q->mutex);

	for(;;) 
	{
		if(quit) 
		{
			ret = -1; break;
		}

		pkt1 = q->first_pkt;
		if (pkt1) 
		{
			q->first_pkt = pkt1->next;

			if (!q->first_pkt)
				q->last_pkt = NULL;

			q->nb_packets--;
			q->size -= pkt1->pkt.size;

			*pkt = pkt1->pkt;
			av_free(pkt1);
			ret = 1;
			break;
		} 
		else if (!block) 
		{
			ret = 0; break;
		} 
		else 
		{
			SDL_CondWait(q->cond, q->mutex);
		}
	}

	SDL_UnlockMutex(q->mutex);
	return ret;
}