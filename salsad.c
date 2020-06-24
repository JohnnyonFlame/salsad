#include <stdio.h>
#include <getopt.h>
#include <alsa/asoundlib.h>

void help()
{
    int card_id = -1;

    fprintf(stderr, "Usage: salsad CARD\n");
    fprintf(stderr, "       salsad hw:x\n");
    fprintf(stderr, "Possible cards:\n");
    fprintf(stderr, " * default\n");
    while (snd_card_next(&card_id) == 0)
    {
        if (card_id < 0)
            break;

        char *name;
        if (snd_card_get_name(card_id, &name) >= 0)
        {
            fprintf(stderr, " * %s\n", name);
            free(name);
        }
    }
}

#define DO_CHECK_RC(rc, func, ...)                              \
    if ((rc = func(__VA_ARGS__)) < 0)                           \
    {                                                           \
        fprintf(stderr, #func ": %s\n", snd_strerror(card_id)); \
        exit(-1);                                               \
    }

#define DO_CHECK_ERR(...) DO_CHECK_RC(err, __VA_ARGS__)

snd_mixer_elem_t *lookup_selem(snd_mixer_t *mixer, snd_mixer_selem_id_t *id, const char *name)
{
    snd_mixer_selem_id_set_index(id, 0);
    snd_mixer_selem_id_set_name(id, name);

    return snd_mixer_find_selem(mixer, id);
}

int is_active_prior(snd_ctl_t *ctl)
{
    snd_ctl_elem_value_t *hp_sense;
    snd_ctl_elem_value_alloca(&hp_sense);
    snd_ctl_elem_value_set_interface(hp_sense, SND_CTL_ELEM_IFACE_CARD);
    snd_ctl_elem_value_set_name(hp_sense, "Headphones Jack");
    snd_ctl_elem_read(ctl, hp_sense);

    if (strcmp(snd_ctl_elem_value_get_name(hp_sense), "Headphones Jack") != 0)
    {
        fprintf(stderr, "is_active_prior: 'Headphones Jack' element not found.\n");
        exit(-1);
    }

    int ret = snd_ctl_elem_value_get_boolean(hp_sense, 0);
    snd_ctl_elem_value_free(hp_sense);
    return ret;
}

void toggle_outputs(snd_mixer_elem_t *hp, snd_mixer_elem_t *sp, int headphones_on)
{
    if (hp)
        snd_mixer_selem_set_playback_switch_all(hp, headphones_on);
    if (sp)
        snd_mixer_selem_set_playback_switch_all(sp, !headphones_on);
}

int find_snd_card(const char *name)
{
    int card_id = -1;
    while (snd_card_next(&card_id) == 0)
    {
        if (card_id < 0)
            break;

        char *card_name;
        if (snd_card_get_name(card_id, &card_name) >= 0)
        {
            int cmp = strcmp(name, card_name);
            free(card_name);

            if (cmp == 0)
                return card_id;
        }
    }

    return -1;
}

int main(int argc, char *argv[])
{
    char *cardname = NULL;
    int c, option_index = 0;
    static struct option long_options[] = {
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0}};
    
    while ((c = getopt_long(argc, argv, "-h", long_options, &option_index)) != -1)
    {
        switch (c)
        {
        case '\1': /* filename (-) */
            cardname = optarg;
            break;

        case 'h': /* -h/--help (h) */
            help();
            return 0;

        default: /* Unknown option */
            return 1;
        }
    }

    if (cardname == NULL) {
        help();
        return 1;
    }

    int err;
    int card_id;
    char card[64] = "";
    snd_ctl_t *ctl;
    snd_mixer_t *mixer;

    // Try finding the card or try querying for it's index
    // If any of those succeed, build hw:xx from that.
    if (((card_id = find_snd_card(cardname)) >= 0) 
        || (card_id = snd_card_get_index(cardname) >= 0))
    {
        snprintf(card, sizeof(card), "hw:%i", card_id);
        DO_CHECK_ERR(snd_ctl_open, &ctl, card, SND_CTL_ASYNC);
    }
    else if (strncmp(cardname, "hw", 2))
    {
        // Name not accepted. Open it directly.
        DO_CHECK_ERR(snd_ctl_open, &ctl, cardname, SND_CTL_ASYNC);
        snprintf(card, sizeof(card), cardname);
    }
    else
    {
        fprintf(stderr, "Failed to find or open sound card.\n");
        exit(-1);
    }

    // Open snd control as blocking and subscribe for receiving events
    DO_CHECK_ERR(snd_ctl_subscribe_events, ctl, 1);

    // Open and initialize sound mixer
    DO_CHECK_ERR(snd_mixer_open, &mixer, 0);
    DO_CHECK_ERR(snd_mixer_attach, mixer, card);
    DO_CHECK_ERR(snd_mixer_selem_register, mixer, NULL, NULL);
    DO_CHECK_ERR(snd_mixer_load, mixer);

    snd_mixer_selem_id_t *id;
    snd_mixer_elem_t *sp, *hp;

    snd_mixer_selem_id_alloca(&id);
    sp = lookup_selem(mixer, id, "Speaker");
    hp = lookup_selem(mixer, id, "Headphones");
    if (!sp)
    {
        fprintf(stderr, "Failure to acquire speaker mixer control! %p, %p\n", sp, hp);
        exit(-1);
    }

    // Change value once first, so we don't end up in a bad state
    toggle_outputs(hp, sp, is_active_prior(ctl));

    snd_ctl_event_t *ev;
    snd_ctl_elem_id_t *val_src;
    snd_ctl_elem_value_t *val;
    snd_ctl_event_alloca(&ev);
    snd_ctl_elem_id_malloc(&val_src);
    snd_ctl_elem_value_alloca(&val);
    for (;;)
    {
        if (snd_ctl_wait(ctl, 1) >= 0)
        {
            if ((err = snd_ctl_read(ctl, ev)) < 0)
            {
                fprintf(stderr, "snd_ctl_read: %s\n", snd_strerror(err));
                break;
            }

            snd_ctl_event_type_t ev_type = snd_ctl_event_get_type(ev);
            if (ev_type != SND_CTL_EVENT_ELEM)
            {
                fprintf(stderr, "Unexpected event type %d.\n", ev_type);
                break;
            }

            snd_ctl_elem_iface_t iface = snd_ctl_event_elem_get_interface(ev);
            unsigned int mask = snd_ctl_event_elem_get_mask(ev);

            //This is probably a Jack insert event
            if (mask == SND_CTL_EVENT_MASK_VALUE && iface == SND_CTL_ELEM_IFACE_CARD)
            {
                const char *ev_name = snd_ctl_event_elem_get_name(ev);
                snd_ctl_event_elem_get_id(ev, val_src);
                snd_ctl_elem_value_set_id(val, val_src);

                if (strcmp(ev_name, "Headphones Jack") == 0)
                {
                    DO_CHECK_ERR(snd_ctl_elem_read, ctl, val);
                    int sense = snd_ctl_elem_value_get_boolean(val, 0);

                    // Headphones Jack is active low
                    toggle_outputs(hp, sp, sense);
                }
            }
        }
    }

    snd_ctl_subscribe_events(ctl, 0);
    snd_ctl_event_free(ev);
    snd_ctl_elem_id_free(val_src);
    snd_ctl_elem_value_free(val);
    return -1;
}